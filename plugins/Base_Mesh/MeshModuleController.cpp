#include "MeshModuleController.h"

#include <EventManager.h>
#include <ContextMenuHub.h>
#include <BarElementPlacementWidget.h>
#include <GridEditingWidget.h>
#include <MeshGlobalMeshingWidget.h>
#include <MeshGroupingWidget.h>
#include <MeshImportSettingsWidget.h>
#include <MeshQualityCheckWidget.h>
#include <MeshSingleMeshingWidget.h>
#include "MeshSmeshSubmeshWidget.h"
#include "MeshSmeshSweepWidget.h"
#include "MeshSmeshPartitionWidget.h"
#include <PickManager.h>
#include <ProjectNode.h>
#include <PublicConstants.h>
#include <PublicVariables.h>
#include <SolverFilePaths.h>
#include <ServiceRegistry.h>
#include <qfiledialog.h>
#include <services/OutputService.h>
#include <services/PreProcessingService.h>
#include <services/MaterialManagerService.h>
#include <services/ProjectManagerService.h>
#include <services/UiFrameService.h>
#include <services/VisualWidgetService.h>
#include <vtkActor.h>
#include <vtkAbstractArray.h>
#include <vtkAppendFilter.h>
#include <vtkCell.h>
#include <vtkCellArray.h>
#include <vtkCellData.h>
#include <vtkCellType.h>
#include <vtkDataSetMapper.h>
#include <vtkDataArray.h>
#include <vtkDataSet.h>
#include <vtkDataSetAttributes.h>
#include <vtkDataSetSurfaceFilter.h>
#include <vtkExtractCells.h>
#include <vtkFieldData.h>
#include <vtkIdList.h>
#include <vtkIdTypeArray.h>
#include <vtkIntArray.h>
#include <vtkLookupTable.h>
#include <vtkMapper.h>
#include <vtkPointLocator.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkProperty.h>
#include <vtkUnstructuredGrid.h>
#include <vtkUnstructuredGridReader.h>
#include <vtkXMLPolyDataReader.h>
#include <vtkXMLPolyDataWriter.h>
#include <vtkXMLUnstructuredGridReader.h>
#include <vtkXMLUnstructuredGridWriter.h>
#include <vtkStringArray.h>
#include <vtkVariant.h>

#include <QByteArray>
#include <QColor>
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QTimer>
#include <QSet>
#include <QStringList>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVector>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <vector>


#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace
{

constexpr int MIN_EDGE_POINTS   = 2;
constexpr int MIN_FACE_POINTS   = 3;
constexpr int MAX_MESH_DIM      = 3;
constexpr int FIRST_COMPUTABLE_MATYPE = 2;
constexpr int FIRST_AUTO_MATYPE = 3;
constexpr int MAX_SOLVER_MATYPE = 300;
constexpr double CENTER_FACTOR  = 0.5;
constexpr double DEFAULT_ALPHA  = 1.0;

struct GridQualityRuntime
{
    MeshQualityCheckOptions options;
    QByteArray outputBytes;
    QSet<QString> answeredPrompts;
    bool finalAnswerSent = false;
    bool iiidirRestored = false;
    bool reportedError = false;
    int highestStep = -1;
    int unsupportedElementType = -1;
};

static const QStringList& gridQualityCaseNames()
{
    static const QStringList names = {
        QStringLiteral("全面检查"),
        QStringLiteral("研究对象合理性检查"),
        QStringLiteral("垂直轴合理性检查"),
        QStringLiteral("单元材料检查"),
        QStringLiteral("单元类型合理性检查"),
        QStringLiteral("单元施工层检查"),
        QStringLiteral("单元重复性检查"),
        QStringLiteral("单元其他属性合理性检查"),
        QStringLiteral("杆单元端点位置检查"),
        QStringLiteral("线对线及点对点单元检查"),
        QStringLiteral("共线不合理节点检查"),
        QStringLiteral("同坐标不同节点编号检查"),
        QStringLiteral("计算范围检查"),
        QStringLiteral("土坝计算层检查"),
        QStringLiteral("单元节点排序检查"),
        QStringLiteral("雅可比求逆检查"),
        QStringLiteral("夹层单元厚度检查"),
        QStringLiteral("单元长细比检查"),
        QStringLiteral("土坝悬空填筑单元检查"),
        QStringLiteral("材料参数合理性检查")};
    return names;
}

static QString gridQualityCaseDisplayName(int checkCase)
{
    const auto& names = gridQualityCaseNames();
    if (checkCase < 0 || checkCase >= names.size())
        return QString::number(checkCase);
    return QStringLiteral("%1：%2").arg(checkCase).arg(names.at(checkCase));
}

static QString gridQualityObjectDisplayName(int objectType)
{
    switch (objectType)
    {
    case 6: return QStringLiteral("洞室");
    case 8: return QStringLiteral("均质坝");
    case 7: return QStringLiteral("边坡");
    default: return QStringLiteral("洞室");
    }
}

static bool meshDomainKeyIsValid(const DomainKey& key)
{
    return !key.category.trimmed().isEmpty() && key.dim >= 0 && key.id != 0;
}

static const char* kMeshMapFileName = PublicConstants::kMeshMapFileName;
static constexpr char kMeshBinaryCacheFileName[] = u8"工程数据/mesh_snapshot.cache.vtu";
static constexpr char kMeshSurfaceCacheFileName[] = u8"工程数据/mesh_surface_mapped_v2.cache.vtp";
static constexpr char kMeshLegacyOuterSurfaceCacheFileName[] = u8"工程数据/mesh_surface.cache.vtp";
static constexpr int kMeshDisplayCacheVersion = 3;
static constexpr char kMeshLineCacheFileName[] = u8"工程数据/mesh_line.cache.vtu";
// 与 MeshGridIO 的 IIIN 退化拓扑恢复规则同步。旧二进制缓存没有此版本标记，
// 或版本较低时，必须从 IIIN 权威快照重新解析，避免旧 TOPOLOGY_TYPE 锁死。
static constexpr char kIiinTopologySchemaVersionArrayName[] = "IIIN_TOPOLOGY_SCHEMA_VERSION";
static constexpr int kIiinTopologySchemaVersion = 2;

// 大网格保存优化：
// 1) IIIN/DAT 网格快照只有在网格对象真正变更后才重写；
// 2) MATYPE/LAYER 来自 vtkCellData，保存工程时不再把每个单元 id 全量写入 mesh_map.json；
// 3) cell_id_to_domain_tags 是运行期反查缓存，可由 tagSets + 网格 CellData 重建，不再落盘。
static QHash<QString, unsigned long> s_savedSnapshotMTimeByPath;

static QString normalizedProjectPathKey(const QString& path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

static unsigned long gridModifiedTime(vtkUnstructuredGrid* grid)
{
    return grid ? static_cast<unsigned long>(grid->GetMTime()) : 0UL;
}

static bool shouldRewriteGridSnapshot(const QString& snapshotPath, vtkUnstructuredGrid* grid)
{
    if (!grid || grid->GetNumberOfCells() <= 0)
        return false;

    if (!QFileInfo::exists(snapshotPath))
        return true;

    const QString key = normalizedProjectPathKey(snapshotPath);
    return s_savedSnapshotMTimeByPath.value(key, 0UL) != gridModifiedTime(grid);
}

static void markGridSnapshotSaved(const QString& snapshotPath, vtkUnstructuredGrid* grid)
{
    if (!grid)
        return;

    s_savedSnapshotMTimeByPath.insert(normalizedProjectPathKey(snapshotPath), gridModifiedTime(grid));
}

static bool meshCacheIsFresh(const QString& cachePath, const QString& sourcePath)
{
    const QFileInfo cacheInfo(cachePath);
    const QFileInfo sourceInfo(sourcePath);
    if (!cacheInfo.exists() || !cacheInfo.isFile() || cacheInfo.size() <= 0)
        return false;
    if (!sourceInfo.exists() || !sourceInfo.isFile())
        return false;
    return cacheInfo.lastModified() >= sourceInfo.lastModified();
}

static bool meshPathIsAscii(const QString& path)
{
    for (const QChar ch : path)
        if (ch.unicode() > 0x7f)
            return false;
    return true;
}

static QString meshShortPathName(const QString& path)
{
#ifdef Q_OS_WIN
    if (meshPathIsAscii(path))
        return path;
    const DWORD length = GetShortPathNameW(reinterpret_cast<LPCWSTR>(path.utf16()), nullptr, 0);
    if (length == 0)
        return QString();
    std::wstring buffer(static_cast<size_t>(length), L'\0');
    const DWORD written = GetShortPathNameW(reinterpret_cast<LPCWSTR>(path.utf16()),
                                            buffer.data(), length);
    if (written == 0 || written >= length)
        return QString();
    buffer.resize(written);
    return QString::fromWCharArray(buffer.data(), static_cast<qsizetype>(buffer.size()));
#else
    return path;
#endif
}

static QString meshVtkReadablePath(const QString& path)
{
    if (meshPathIsAscii(path))
        return path;
    const QString shortPath = meshShortPathName(path);
    return shortPath.isEmpty() ? path : shortPath;
}

static QString meshVtkWritablePath(const QString& path)
{
    if (meshPathIsAscii(path))
        return path;

    const QFileInfo info(path);
    const QString shortDir = meshShortPathName(info.absolutePath());
    if (!shortDir.isEmpty())
        return QDir(shortDir).filePath(info.fileName());
    return path;
}

static bool meshGridHasCurrentIiinTopologySchema(vtkUnstructuredGrid* grid)
{
    if (!grid || !grid->GetFieldData() || !grid->GetCellData())
        return false;

    auto* version = vtkIntArray::SafeDownCast(
        grid->GetFieldData()->GetArray(kIiinTopologySchemaVersionArrayName));
    if (!version || version->GetNumberOfTuples() < 1 ||
        version->GetValue(0) != kIiinTopologySchemaVersion)
    {
        return false;
    }

    vtkDataArray* topology = grid->GetCellData()->GetArray("TOPOLOGY_TYPE");
    vtkDataArray* pattern = grid->GetCellData()->GetArray("IIIN_CONNECTIVITY_PATTERN");
    const vtkIdType cellCount = grid->GetNumberOfCells();
    return topology && pattern &&
           topology->GetNumberOfTuples() == cellCount &&
           pattern->GetNumberOfTuples() == cellCount;
}

static bool ensureMeshCacheParentDirectory(const QString& path)
{
    const QString parentPath = QFileInfo(path).absolutePath();
    if (parentPath.isEmpty())
        return false;

    QDir parent(parentPath);
    if (parent.exists())
        return true;

    return QDir().mkpath(parentPath) && QDir(parentPath).exists();
}

// Some VTK builds on Windows still open XML files through a narrow-char path.
// When the project contains Chinese directory names and no usable 8.3 short path
// exists, let VTK work on an ASCII staging file and let Qt move/copy it to the
// real Unicode path afterwards.
static QString meshAsciiStagingPath(const QString& targetPath)
{
    QDir dir(QFileInfo(targetPath).absolutePath());
    while (true)
    {
        const QString candidate = dir.absolutePath();
        if (meshPathIsAscii(candidate) && QFileInfo(candidate).isDir())
        {
            const QString fileName =
                QStringLiteral(".geocae_vtk_%1_%2_%3")
                    .arg(QCoreApplication::applicationPid())
                    .arg(QString::number(qHash(QFileInfo(targetPath).absoluteFilePath()), 16))
                    .arg(QFileInfo(targetPath).fileName());
            return dir.filePath(fileName);
        }

        if (!dir.cdUp())
            break;
    }
    return QString();
}

static QString prepareMeshVtkReadablePath(const QString& path, bool& staged)
{
    staged = false;
    const QString vtkPath = meshVtkReadablePath(path);
    if (meshPathIsAscii(vtkPath))
        return vtkPath;

    const QString stagingPath = meshAsciiStagingPath(path);
    if (stagingPath.isEmpty())
        return vtkPath;

    QFile::remove(stagingPath);
    if (!QFile::copy(path, stagingPath))
        return vtkPath;

    staged = true;
    return stagingPath;
}

static QString prepareMeshVtkWritablePath(const QString& path, bool& staged)
{
    staged = false;
    if (!ensureMeshCacheParentDirectory(path))
        return QString();

    const QString vtkPath = meshVtkWritablePath(path);
    if (meshPathIsAscii(vtkPath))
        return vtkPath;

    const QString stagingPath = meshAsciiStagingPath(path);
    if (stagingPath.isEmpty())
        return vtkPath;

    QFile::remove(stagingPath);
    staged = true;
    return stagingPath;
}

static bool commitMeshVtkStagingFile(const QString& stagingPath, const QString& targetPath)
{
    QFile::remove(targetPath);

    // Same-volume rename is normally enough. Keep copy as a safe fallback.
    if (QFile::rename(stagingPath, targetPath))
        return true;

    if (QFile::copy(stagingPath, targetPath))
    {
        QFile::remove(stagingPath);
        return true;
    }

    QFile::remove(stagingPath);
    return false;
}

static vtkSmartPointer<vtkUnstructuredGrid> readMeshGridCache(const QString& path)
{
    if (!QFileInfo::exists(path))
        return nullptr;

    bool staged = false;
    const QString readablePath = prepareMeshVtkReadablePath(path, staged);
    auto reader = vtkSmartPointer<vtkXMLUnstructuredGridReader>::New();
    const QByteArray encoded = QFile::encodeName(readablePath);
    reader->SetFileName(encoded.constData());
    reader->Update();
    if (staged)
        QFile::remove(readablePath);

    vtkUnstructuredGrid* output = reader->GetOutput();
    if (!output || output->GetNumberOfCells() <= 0 || output->GetNumberOfPoints() <= 0)
        return nullptr;
    auto grid = vtkSmartPointer<vtkUnstructuredGrid>::New();
    grid->ShallowCopy(output);
    return grid;
}

static vtkSmartPointer<vtkPolyData> readMeshSurfaceCache(const QString& path)
{
    if (!QFileInfo::exists(path))
        return nullptr;

    bool staged = false;
    const QString readablePath = prepareMeshVtkReadablePath(path, staged);
    auto reader = vtkSmartPointer<vtkXMLPolyDataReader>::New();
    const QByteArray encoded = QFile::encodeName(readablePath);
    reader->SetFileName(encoded.constData());
    reader->Update();
    if (staged)
        QFile::remove(readablePath);

    vtkPolyData* output = reader->GetOutput();
    if (!output || output->GetNumberOfCells() <= 0 || output->GetNumberOfPoints() <= 0)
        return nullptr;
    auto surface = vtkSmartPointer<vtkPolyData>::New();
    surface->ShallowCopy(output);
    return surface;
}

static bool writeMeshGridCache(const QString& path, vtkUnstructuredGrid* grid)
{
    if (!grid || grid->GetNumberOfCells() <= 0)
        return false;

    bool staged = false;
    const QString writablePath = prepareMeshVtkWritablePath(path, staged);
    if (writablePath.isEmpty())
        return false;

    auto writer = vtkSmartPointer<vtkXMLUnstructuredGridWriter>::New();
    const QByteArray encoded = QFile::encodeName(writablePath);
    writer->SetFileName(encoded.constData());
    writer->SetInputData(grid);
    writer->SetDataModeToBinary();
    const bool ok = writer->Write() == 1;

    if (!staged)
        return ok;
    if (!ok)
    {
        QFile::remove(writablePath);
        return false;
    }
    return commitMeshVtkStagingFile(writablePath, path);
}

static bool writeMeshSurfaceCache(const QString& path, vtkPolyData* surface)
{
    if (!surface || surface->GetNumberOfCells() <= 0)
        return false;

    bool staged = false;
    const QString writablePath = prepareMeshVtkWritablePath(path, staged);
    if (writablePath.isEmpty())
        return false;

    auto writer = vtkSmartPointer<vtkXMLPolyDataWriter>::New();
    const QByteArray encoded = QFile::encodeName(writablePath);
    writer->SetFileName(encoded.constData());
    writer->SetInputData(surface);
    writer->SetDataModeToBinary();
    const bool ok = writer->Write() == 1;

    if (!staged)
        return ok;
    if (!ok)
    {
        QFile::remove(writablePath);
        return false;
    }
    return commitMeshVtkStagingFile(writablePath, path);
}

static double defaultNumericArrayValue(const QString& name, vtkIdType tuple)
{
    if (name.compare(QStringLiteral("MATYPE"), Qt::CaseInsensitive) == 0)
        return 2.0;
    if (name.compare(QStringLiteral("LAYER"), Qt::CaseInsensitive) == 0)
        return 1.0;
    if (name.compare(QStringLiteral("node_id"), Qt::CaseInsensitive) == 0 ||
        name.compare(QStringLiteral("elem_id"), Qt::CaseInsensitive) == 0)
    {
        return static_cast<double>(tuple + 1);
    }
    if (name.startsWith(QStringLiteral("vtkOriginal"), Qt::CaseInsensitive))
        return static_cast<double>(tuple);
    return 0.0;
}

static bool normalizeAttributeTupleCounts(vtkDataSetAttributes* attributes,
                                          vtkIdType expectedTuples,
                                          const QString& association,
                                          bool reportRepairs)
{
    if (!attributes || expectedTuples < 0)
        return false;

    bool changed = false;
    for (int arrayIndex = 0; arrayIndex < attributes->GetNumberOfArrays(); ++arrayIndex)
    {
        vtkAbstractArray* abstractArray = attributes->GetAbstractArray(arrayIndex);
        if (!abstractArray)
            continue;
        const vtkIdType oldTuples = abstractArray->GetNumberOfTuples();
        if (oldTuples == expectedTuples)
            continue;

        const QString name = QString::fromUtf8(
            abstractArray->GetName() ? abstractArray->GetName() : "未命名");
        const int components = qMax(1, abstractArray->GetNumberOfComponents());
        // SetNumberOfTuples() may reallocate without copying the old buffer.
        // Resize() explicitly preserves existing values; Insert* then advances
        // the valid range while initializing only newly added tuples.
        if (!abstractArray->Resize(expectedTuples))
        {
            if (reportRepairs)
            {
                qWarning().noquote()
                    << QStringLiteral("__RAW__[网格数据修复] %1数组“%2”无法调整为 %3 个元组。")
                           .arg(association, name)
                           .arg(expectedTuples);
            }
            continue;
        }

        if (expectedTuples > oldTuples)
        {
            if (vtkDataArray* dataArray = vtkDataArray::SafeDownCast(abstractArray))
            {
                for (vtkIdType tuple = oldTuples; tuple < expectedTuples; ++tuple)
                {
                    const double defaultValue = defaultNumericArrayValue(name, tuple);
                    for (int component = 0; component < components; ++component)
                        dataArray->InsertComponent(tuple, component, defaultValue);
                }
            }
            else if (auto* stringArray = vtkStringArray::SafeDownCast(abstractArray))
            {
                for (vtkIdType value = oldTuples * components;
                     value < expectedTuples * components; ++value)
                {
                    stringArray->InsertValue(value, std::string());
                }
            }
            else
            {
                for (vtkIdType value = oldTuples * components;
                     value < expectedTuples * components; ++value)
                {
                    abstractArray->InsertVariantValue(value, vtkVariant());
                }
            }
        }

        if (reportRepairs)
        {
            qWarning().noquote()
                << QStringLiteral("__RAW__[网格数据修复] %1数组“%2”的元组数由 %3 调整为 %4。")
                       .arg(association, name)
                       .arg(oldTuples)
                       .arg(expectedTuples);
        }
        changed = true;
    }
    return changed;
}

static bool normalizeGridAttributeTupleCounts(vtkUnstructuredGrid* grid,
                                              bool reportRepairs = true,
                                              bool recoverBarGroups = true)
{
    if (!grid)
        return false;
    vtkDataArray* barGroups = grid->GetCellData()->GetArray("BAR_GROUP");
    auto* barGroupNames = vtkStringArray::SafeDownCast(
        grid->GetCellData()->GetAbstractArray("BAR_GROUP_NAME"));
    const vtkIdType oldBarGroupTuples =
        barGroups ? barGroups->GetNumberOfTuples() : grid->GetNumberOfCells();
    const bool pointChanged = normalizeAttributeTupleCounts(
        grid->GetPointData(), grid->GetNumberOfPoints(), QStringLiteral("节点"), reportRepairs);
    const bool cellChanged = normalizeAttributeTupleCounts(
        grid->GetCellData(), grid->GetNumberOfCells(), QStringLiteral("单元"), reportRepairs);
    int recoveredBarCells = 0;
    if (recoverBarGroups && barGroups && oldBarGroupTuples > 0 &&
        oldBarGroupTuples < grid->GetNumberOfCells())
    {
        for (vtkIdType cid = oldBarGroupTuples; cid < grid->GetNumberOfCells(); ++cid)
        {
            vtkCell* cell = grid->GetCell(cid);
            if (!cell || cell->GetCellDimension() != 1 || cid <= 0)
                continue;
            vtkCell* previousCell = grid->GetCell(cid - 1);
            const int previousGroup = static_cast<int>(barGroups->GetTuple1(cid - 1));
            if (!previousCell || previousCell->GetCellDimension() != 1 || previousGroup <= 0)
                continue;
            barGroups->SetTuple1(cid, previousGroup);
            if (barGroupNames && cid < barGroupNames->GetNumberOfValues() &&
                cid - 1 < barGroupNames->GetNumberOfValues())
            {
                barGroupNames->SetValue(cid, barGroupNames->GetValue(cid - 1));
            }
            ++recoveredBarCells;
        }
    }
    if (reportRepairs && recoveredBarCells > 0)
    {
        qWarning().noquote()
            << QStringLiteral("__RAW__[网格数据修复] 已从相邻杆单元恢复 %1 个生成记录标记。")
                   .arg(recoveredBarCells);
    }
    if (pointChanged || cellChanged)
        grid->Modified();
    return pointChanged || cellChanged;
}

static bool isGridCellDataDomainTag(const MeshTagKey& key)
{
    return key.category.compare(QStringLiteral("MATYPE"), Qt::CaseInsensitive) == 0
           || key.category.compare(QStringLiteral("LAYER"), Qt::CaseInsensitive) == 0;
}

static bool copyFileOverwrite(const QString& sourcePath, const QString& targetPath)
{
    if (sourcePath.trimmed().isEmpty() || targetPath.trimmed().isEmpty())
        return false;
    if (!QFileInfo::exists(sourcePath))
        return false;

    if (QFileInfo::exists(targetPath) && !QFile::remove(targetPath))
        return false;

    return QFile::copy(sourcePath, targetPath);
}

static vtkSmartPointer<vtkUnstructuredGrid> readVtkUnstructuredGrid(const QString& fileName,
                                                                    QString* errMsg)
{
    if (errMsg)
        errMsg->clear();

    auto reader = vtkSmartPointer<vtkUnstructuredGridReader>::New();
    reader->SetFileName(QFile::encodeName(fileName).constData());
    reader->Update();

    auto* output = reader->GetOutput();
    if (!output || output->GetNumberOfCells() <= 0)
    {
        if (errMsg)
            *errMsg = QStringLiteral("VTK mesh is empty or invalid: %1").arg(fileName);
        return nullptr;
    }

    auto grid = vtkSmartPointer<vtkUnstructuredGrid>::New();
    grid->DeepCopy(output);
    MeshGridIO::setVerticalAxis(grid, 2);
    return grid;
}

static QString physicalUiNameByDim(int dim, int id)
{
    QString prefix;
    switch (dim)
    {
    case 0: prefix = QStringLiteral("点边界"); break;
    case 1: prefix = QStringLiteral("线边界"); break;
    case 2: prefix = QStringLiteral("面边界"); break;
    case 3: prefix = QStringLiteral("体域"); break;
    default: prefix = QStringLiteral("分组"); break;
    }
    return QStringLiteral("%1%2").arg(prefix).arg(id);
}

static QString normalizePhysicalUiName(int dim, int id, const QString& rawName)
{
    const QString name = rawName.trimmed();
    if (name.isEmpty())
        return physicalUiNameByDim(dim, id);

    static const QRegularExpression generatedNameRe(
        QStringLiteral("^(dim\\d+(_|-)?.*|phys_\\d+|point_\\d+|edge_\\d+|face_\\d+|volume_\\d+|MeshPoint_\\d+|MeshEdge_\\d+|MeshFace_\\d+|MeshVolume_\\d+)$"),
        QRegularExpression::CaseInsensitiveOption);
    if (generatedNameRe.match(name).hasMatch())
        return physicalUiNameByDim(dim, id);

    return name;
}

static QVector<MeshIdType> meshIdsFromIntVector(const QVector<int>& values)
{
    QVector<MeshIdType> out;
    out.reserve(values.size());
    for (int v : values)
        out.push_back(static_cast<MeshIdType>(v));

    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

static QVector<MeshIdType> meshIdsFromInt64Vector(const QVector<qint64>& values)
{
    QVector<MeshIdType> out;
    out.reserve(values.size());
    for (qint64 v : values)
        out.push_back(static_cast<MeshIdType>(v));

    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

static void normalizeMeshTagSet(MeshTagSet& set)
{
    set.name = normalizePhysicalUiName(set.key.dim, set.key.id, set.name);

    std::sort(set.pointIds.begin(), set.pointIds.end());
    set.pointIds.erase(std::unique(set.pointIds.begin(), set.pointIds.end()), set.pointIds.end());

    std::sort(set.cellIds.begin(), set.cellIds.end());
    set.cellIds.erase(std::unique(set.cellIds.begin(), set.cellIds.end()), set.cellIds.end());

    std::sort(set.edgeKeys.begin(), set.edgeKeys.end());
    set.edgeKeys.erase(std::unique(set.edgeKeys.begin(), set.edgeKeys.end()), set.edgeKeys.end());

    std::sort(set.faceKeys.begin(), set.faceKeys.end());
    set.faceKeys.erase(std::unique(set.faceKeys.begin(), set.faceKeys.end()), set.faceKeys.end());
}

static void upsertPhysicalBoundarySet(QHash<MeshTagKey, MeshTagSet>& sets,
                                      const MshPickMap& map,
                                      int physId,
                                      int dim,
                                      const QVector<int>& pointIds,
                                      const QVector<qint64>& edgeKeys,
                                      const QVector<qint64>& faceKeys)
{
    if (physId <= 0 || dim < 0 || dim >= map.meshDim)
        return;
    if (pointIds.isEmpty() && edgeKeys.isEmpty() && faceKeys.isEmpty())
        return;

    MeshTagKey key;
    key.category = QStringLiteral("Physical");
    key.dim = dim;
    key.id = physId;

    MeshTagSet set = sets.value(key);
    set.key = key;
    if (set.source.trimmed().isEmpty())
        set.source = QStringLiteral("BoundaryIndex");

    if (set.name.trimmed().isEmpty())
        set.name = map.physIdToName.value(physId);

    set.pointIds += meshIdsFromIntVector(pointIds);
    set.edgeKeys += meshIdsFromInt64Vector(edgeKeys);
    set.faceKeys += meshIdsFromInt64Vector(faceKeys);

    normalizeMeshTagSet(set);
    sets.insert(key, set);
}

static QVector<MeshTagSet> sortedMeshTagSets(QHash<MeshTagKey, MeshTagSet> sets)
{
    QVector<MeshTagSet> out = sets.values().toVector();

    std::sort(out.begin(), out.end(), [](const MeshTagSet& a, const MeshTagSet& b)
              {
                  if (a.key.dim != b.key.dim)
                      return a.key.dim < b.key.dim;
                  if (a.key.category != b.key.category)
                      return a.key.category < b.key.category;
                  return a.key.id < b.key.id;
              });

    return out;
}


static QJsonArray meshIdVectorToJson(const QVector<MeshIdType>& ids)
{
    QJsonArray arr;
    for (MeshIdType id : ids)
        arr.append(QString::number(static_cast<qint64>(id)));
    return arr;
}

static QVector<MeshIdType> jsonToMeshIdVector(const QJsonArray& arr)
{
    QVector<MeshIdType> ids;
    ids.reserve(arr.size());
    for (const auto& v : arr)
        ids.push_back(static_cast<MeshIdType>(v.toString().toLongLong()));
    return ids;
}

static QJsonArray intVectorToJson(const QVector<int>& ids)
{
    QJsonArray arr;
    for (int id : ids)
        arr.append(id);
    return arr;
}

static QVector<int> jsonToIntVector(const QJsonArray& arr)
{
    QVector<int> ids;
    ids.reserve(arr.size());
    for (const auto& v : arr)
        ids.push_back(v.toInt());
    return ids;
}

static QJsonArray int64VectorToJson(const QVector<qint64>& ids)
{
    QJsonArray arr;
    for (qint64 id : ids)
        arr.append(QString::number(id));
    return arr;
}

static QVector<qint64> jsonToInt64Vector(const QJsonArray& arr)
{
    QVector<qint64> ids;
    ids.reserve(arr.size());
    for (const auto& v : arr)
        ids.push_back(v.toString().toLongLong());
    return ids;
}

static QJsonArray nestedIntVectorToJson(const QVector<QVector<int>>& values)
{
    QJsonArray arr;

    for (const QVector<int>& one : values)
    {
        QJsonArray item;
        for (int v : one)
            item.append(v);
        arr.append(item);
    }

    return arr;
}

static QVector<QVector<int>> jsonToNestedIntVector(const QJsonArray& arr)
{
    QVector<QVector<int>> values;
    values.reserve(arr.size());

    for (const auto& v : arr)
    {
        QVector<int> one;
        const QJsonArray item = v.toArray();
        one.reserve(item.size());

        for (const auto& x : item)
            one.push_back(x.toInt());

        values.push_back(one);
    }

    return values;
}

static QJsonObject intStringHashToJson(const QHash<int, QString>& map)
{
    QJsonObject obj;
    for (auto it = map.constBegin(); it != map.constEnd(); ++it)
        obj[QString::number(it.key())] = it.value();
    return obj;
}

static QHash<int, QString> jsonToIntStringHash(const QJsonObject& obj)
{
    QHash<int, QString> map;
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it)
        map.insert(it.key().toInt(), it.value().toString());
    return map;
}

static QJsonObject intIntHashToJson(const QHash<int, int>& map)
{
    QJsonObject obj;
    for (auto it = map.constBegin(); it != map.constEnd(); ++it)
        obj[QString::number(it.key())] = it.value();
    return obj;
}

static QHash<int, int> jsonToIntIntHash(const QJsonObject& obj)
{
    QHash<int, int> map;
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it)
        map.insert(it.key().toInt(), it.value().toInt());
    return map;
}

static QJsonObject intVectorHashToJson(const QHash<int, QVector<int>>& map)
{
    QJsonObject obj;
    for (auto it = map.constBegin(); it != map.constEnd(); ++it)
        obj[QString::number(it.key())] = intVectorToJson(it.value());
    return obj;
}

static QHash<int, QVector<int>> jsonToIntVectorHash(const QJsonObject& obj)
{
    QHash<int, QVector<int>> map;
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it)
        map.insert(it.key().toInt(), jsonToIntVector(it.value().toArray()));
    return map;
}

static QJsonObject intInt64VectorHashToJson(const QHash<int, QVector<qint64>>& map)
{
    QJsonObject obj;
    for (auto it = map.constBegin(); it != map.constEnd(); ++it)
        obj[QString::number(it.key())] = int64VectorToJson(it.value());
    return obj;
}

static QHash<int, QVector<qint64>> jsonToIntInt64VectorHash(const QJsonObject& obj)
{
    QHash<int, QVector<qint64>> map;
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it)
        map.insert(it.key().toInt(), jsonToInt64Vector(it.value().toArray()));
    return map;
}

static QJsonObject nestedIntVectorHashToJson(const QHash<int, QVector<QVector<int>>>& map)
{
    QJsonObject obj;
    for (auto it = map.constBegin(); it != map.constEnd(); ++it)
        obj[QString::number(it.key())] = nestedIntVectorToJson(it.value());
    return obj;
}

static QHash<int, QVector<QVector<int>>> jsonToNestedIntVectorHash(const QJsonObject& obj)
{
    QHash<int, QVector<QVector<int>>> map;
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it)
        map.insert(it.key().toInt(), jsonToNestedIntVector(it.value().toArray()));
    return map;
}

static QJsonObject meshTagKeyToJson(const MeshTagKey& key)
{
    QJsonObject obj;
    obj["category"] = key.category;
    obj["dim"] = key.dim;
    obj["id"] = key.id;
    return obj;
}

static MeshTagKey jsonToMeshTagKey(const QJsonObject& obj)
{
    MeshTagKey key;
    key.category = obj.value("category").toString();
    key.dim = obj.value("dim").toInt();
    key.id = obj.value("id").toInt();
    return key;
}

static QJsonArray meshTagKeyVectorToJson(const QVector<MeshTagKey>& keys)
{
    QJsonArray arr;
    for (const MeshTagKey& key : keys)
        arr.append(meshTagKeyToJson(key));
    return arr;
}

static QVector<MeshTagKey> jsonToMeshTagKeyVector(const QJsonArray& arr)
{
    QVector<MeshTagKey> keys;
    keys.reserve(arr.size());

    for (const auto& v : arr)
        keys.push_back(jsonToMeshTagKey(v.toObject()));

    return keys;
}

static QJsonObject meshIdTagKeyVectorHashToJson(const QHash<MeshIdType, QVector<MeshTagKey>>& map)
{
    QJsonObject obj;

    for (auto it = map.constBegin(); it != map.constEnd(); ++it)
    {
        obj[QString::number(static_cast<qint64>(it.key()))] =
            meshTagKeyVectorToJson(it.value());
    }

    return obj;
}

static QHash<MeshIdType, QVector<MeshTagKey>> jsonToMeshIdTagKeyVectorHash(
    const QJsonObject& obj)
{
    QHash<MeshIdType, QVector<MeshTagKey>> map;

    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it)
    {
        const MeshIdType id = static_cast<MeshIdType>(it.key().toLongLong());
        map.insert(id, jsonToMeshTagKeyVector(it.value().toArray()));
    }

    return map;
}

static QJsonObject meshTagSetToJson(const MeshTagSet& set)
{
    QJsonObject obj;

    obj["key"] = meshTagKeyToJson(set.key);
    obj["name"] = set.name;
    obj["source"] = set.source;
    obj["point_ids"] = meshIdVectorToJson(set.pointIds);
    obj["cell_ids"] = meshIdVectorToJson(set.cellIds);
    obj["edge_keys"] = meshIdVectorToJson(set.edgeKeys);
    obj["face_keys"] = meshIdVectorToJson(set.faceKeys);

    return obj;
}

static MeshTagSet jsonToMeshTagSet(const QJsonObject& obj)
{
    MeshTagSet set;

    set.key = jsonToMeshTagKey(obj.value("key").toObject());
    set.name = obj.value("name").toString();
    set.source = obj.value("source").toString();
    set.pointIds = jsonToMeshIdVector(obj.value("point_ids").toArray());
    set.cellIds = jsonToMeshIdVector(obj.value("cell_ids").toArray());
    set.edgeKeys = jsonToMeshIdVector(obj.value("edge_keys").toArray());
    set.faceKeys = jsonToMeshIdVector(obj.value("face_keys").toArray());

    return set;
}

static QHash<int, int> buildPointIdToNodeId(vtkUnstructuredGrid* grid)
{
    QHash<int, int> pointToNode;

    auto* nodeIdArr = grid && grid->GetPointData()
                          ? vtkIntArray::SafeDownCast(grid->GetPointData()->GetArray("node_id"))
                          : nullptr;

    if (!grid || !nodeIdArr)
        return pointToNode;

    const vtkIdType nPts = grid->GetNumberOfPoints();
    for (vtkIdType pid = 0; pid < nPts; ++pid)
        pointToNode.insert(static_cast<int>(pid), nodeIdArr->GetValue(pid));

    return pointToNode;
}

static QHash<int, int> buildNodeIdToPointId(vtkUnstructuredGrid* grid)
{
    QHash<int, int> nodeToPoint;

    auto* nodeIdArr = grid && grid->GetPointData()
                          ? vtkIntArray::SafeDownCast(grid->GetPointData()->GetArray("node_id"))
                          : nullptr;

    if (!grid || !nodeIdArr)
        return nodeToPoint;

    const vtkIdType nPts = grid->GetNumberOfPoints();
    for (vtkIdType pid = 0; pid < nPts; ++pid)
        nodeToPoint.insert(nodeIdArr->GetValue(pid), static_cast<int>(pid));

    return nodeToPoint;
}

static QVector<int> pointIdsToNodeIds(const QVector<int>& pointIds,
                                      const QHash<int, int>& pointToNode)
{
    QVector<int> nodeIds;
    nodeIds.reserve(pointIds.size());

    for (int pid : pointIds)
    {
        if (pointToNode.contains(pid))
            nodeIds.push_back(pointToNode.value(pid));
    }

    std::sort(nodeIds.begin(), nodeIds.end());
    nodeIds.erase(std::unique(nodeIds.begin(), nodeIds.end()), nodeIds.end());

    return nodeIds;
}

static QHash<int, QVector<int>> pointPhysToNodePhys(
    const QHash<int, QVector<int>>& physToPointIds,
    const QHash<int, int>& pointToNode)
{
    QHash<int, QVector<int>> out;

    for (auto it = physToPointIds.constBegin(); it != physToPointIds.constEnd(); ++it)
        out.insert(it.key(), pointIdsToNodeIds(it.value(), pointToNode));

    return out;
}

static QVector<int> nodeIdsToPointIds(const QVector<int>& nodeIds,
                                      const QHash<int, int>& nodeToPoint)
{
    QVector<int> pointIds;
    pointIds.reserve(nodeIds.size());

    for (int nodeId : nodeIds)
    {
        if (nodeToPoint.contains(nodeId))
            pointIds.push_back(nodeToPoint.value(nodeId));
    }

    std::sort(pointIds.begin(), pointIds.end());
    pointIds.erase(std::unique(pointIds.begin(), pointIds.end()), pointIds.end());

    return pointIds;
}

static QHash<int, QVector<int>> nodePhysToPointPhys(
    const QHash<int, QVector<int>>& physToNodeIds,
    const QHash<int, int>& nodeToPoint)
{
    QHash<int, QVector<int>> out;

    for (auto it = physToNodeIds.constBegin(); it != physToNodeIds.constEnd(); ++it)
        out.insert(it.key(), nodeIdsToPointIds(it.value(), nodeToPoint));

    return out;
}


static MeshTagSet& ensurePhysicalTagSet(MshPickMap& pick,
                                        int physId,
                                        int dim,
                                        const QString& source)
{
    static MeshTagSet invalid;
    if (physId <= 0 || dim < 0 || dim > 3)
        return invalid;

    MeshTagKey key{QStringLiteral("Physical"), dim, physId};
    if (!pick.tagSets.contains(key))
    {
        MeshTagSet set;
        set.key = key;
        set.name = pick.physIdToName.value(physId, QStringLiteral("Physical=%1").arg(physId));
        set.source = source;
        pick.tagSets.insert(key, set);
    }

    MeshTagSet& set = pick.tagSets[key];
    if (set.name.trimmed().isEmpty())
        set.name = pick.physIdToName.value(physId, QStringLiteral("Physical=%1").arg(physId));
    if (set.source.trimmed().isEmpty())
        set.source = source;
    pick.physIdToName.insert(physId, set.name);
    pick.physIdToDim.insert(physId, dim);
    return set;
}

static void setPhysicalBoundaryPointIds(MshPickMap& pick,
                                        int physId,
                                        int dim,
                                        const QVector<int>& pointIds,
                                        const QString& source)
{
    MeshTagSet& set = ensurePhysicalTagSet(pick, physId, dim, source);
    if (!set.key.isValid())
        return;
    set.pointIds.clear();
    set.pointIds.reserve(pointIds.size());
    for (int pid : pointIds)
    {
        if (pid >= 0)
            set.pointIds.push_back(static_cast<MeshIdType>(pid));
    }
}

static void appendPhysicalBoundaryEdgeKey(MshPickMap& pick, int physId, qint64 edgeKey, const QString& source)
{
    MeshTagSet& set = ensurePhysicalTagSet(pick, physId, 1, source);
    if (set.key.isValid())
        set.edgeKeys.push_back(static_cast<MeshIdType>(edgeKey));
}

static void appendPhysicalBoundaryFaceKey(MshPickMap& pick, int physId, qint64 faceKey, const QString& source)
{
    MeshTagSet& set = ensurePhysicalTagSet(pick, physId, 2, source);
    if (set.key.isValid())
        set.faceKeys.push_back(static_cast<MeshIdType>(faceKey));
}

static void normalizeMeshTagSets(MshPickMap& pick)
{
    pick.pointIdToBoundaryTags.clear();

    auto dedupMeshIds = [](QVector<MeshIdType>& values)
    {
        std::sort(values.begin(), values.end());
        values.erase(std::unique(values.begin(), values.end()), values.end());
    };

    for (auto it = pick.tagSets.begin(); it != pick.tagSets.end(); ++it)
    {
        MeshTagSet& set = it.value();
        dedupMeshIds(set.pointIds);
        dedupMeshIds(set.cellIds);
        dedupMeshIds(set.edgeKeys);
        dedupMeshIds(set.faceKeys);

        if (set.key.category == QStringLiteral("Physical"))
        {
            if (set.name.trimmed().isEmpty())
                set.name = pick.physIdToName.value(set.key.id, QStringLiteral("Physical=%1").arg(set.key.id));
            pick.physIdToName.insert(set.key.id, set.name);
            pick.physIdToDim.insert(set.key.id, set.key.dim);

            if (set.key.dim >= 0 && set.key.dim < pick.meshDim)
            {
                for (MeshIdType pid : set.pointIds)
                    pick.pointIdToBoundaryTags[pid].push_back(set.key);
            }
        }
    }

    for (auto it = pick.pointIdToBoundaryTags.begin(); it != pick.pointIdToBoundaryTags.end(); ++it)
    {
        auto& keys = it.value();
        std::sort(keys.begin(), keys.end(), [](const MeshTagKey& a, const MeshTagKey& b)
                  {
                      if (a.category != b.category)
                          return a.category < b.category;
                      if (a.dim != b.dim)
                          return a.dim < b.dim;
                      return a.id < b.id;
                  });
        keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    }
}

static QHash<int, QVector<QVector<int>>> edgeNodeElemsFromKeys(
    vtkUnstructuredGrid* grid,
    const MshPickMap& pick,
    const QHash<int, int>& pointToNode)
{
    QHash<int, QVector<QVector<int>>> out;

    if (!grid)
        return out;

    for (const MeshTagSet& set : pick.physicalTagSets(1))
    {
        const int physId = set.key.id;

        for (qint64 edgeKey : set.edgeKeys)
        {
            const vtkIdType cid = static_cast<vtkIdType>(edgeKey >> 4);
            const int localEdgeId = static_cast<int>(edgeKey & 0xF);

            vtkCell* cell = grid->GetCell(cid);
            vtkCell* edge = cell ? cell->GetEdge(localEdgeId) : nullptr;

            if (!edge || edge->GetNumberOfPoints() < MIN_EDGE_POINTS)
                continue;

            const int p0 = static_cast<int>(edge->GetPointId(0));
            const int p1 = static_cast<int>(edge->GetPointId(1));

            if (!pointToNode.contains(p0) || !pointToNode.contains(p1))
                continue;

            QVector<int> nodePair;
            nodePair.push_back(pointToNode.value(p0));
            nodePair.push_back(pointToNode.value(p1));
            out[physId].push_back(nodePair);
        }
    }

    return out;
}

static QHash<int, QVector<QVector<int>>> faceNodeElemsFromKeys(
    vtkUnstructuredGrid* grid,
    const MshPickMap& pick,
    const QHash<int, int>& pointToNode)
{
    QHash<int, QVector<QVector<int>>> out;

    if (!grid)
        return out;

    for (const MeshTagSet& set : pick.physicalTagSets(2))
    {
        const int physId = set.key.id;

        for (qint64 faceKey : set.faceKeys)
        {
            const vtkIdType cid = static_cast<vtkIdType>(faceKey >> 4);
            const int localFaceId = static_cast<int>(faceKey & 0xF);

            vtkCell* cell = grid->GetCell(cid);
            vtkCell* face = cell ? cell->GetFace(localFaceId) : nullptr;

            if (!face || face->GetNumberOfPoints() < MIN_FACE_POINTS)
                continue;

            QVector<int> nodeIds;
            nodeIds.reserve(face->GetNumberOfPoints());

            for (vtkIdType i = 0; i < face->GetNumberOfPoints(); ++i)
            {
                const int pid = static_cast<int>(face->GetPointId(i));
                if (pointToNode.contains(pid))
                    nodeIds.push_back(pointToNode.value(pid));
            }

            if (nodeIds.size() >= 3)
                out[physId].push_back(nodeIds);
        }
    }

    return out;
}

static quint64 packEdgePointIds(int a, int b)
{
    const quint32 x = static_cast<quint32>(qMin(a, b));
    const quint32 y = static_cast<quint32>(qMax(a, b));
    return (quint64(x) << 32) | quint64(y);
}

static QByteArray packFacePointIds(QVector<int> ids)
{
    std::sort(ids.begin(), ids.end());

    QByteArray key;
    key.reserve(ids.size() * int(sizeof(qint32)));

    for (int id : ids)
    {
        const qint32 v = qint32(id);
        key.append(reinterpret_cast<const char*>(&v), int(sizeof(qint32)));
    }

    return key;
}

static void rebuildCurrentNodeMaps(MshPickMap& pick, vtkUnstructuredGrid* grid)
{
    pick.nodeIdToPointId.clear();
    pick.pointIdToNodeId.clear();

    auto* nodeIdArr = grid && grid->GetPointData()
                          ? vtkIntArray::SafeDownCast(grid->GetPointData()->GetArray("node_id"))
                          : nullptr;

    if (!grid || !nodeIdArr)
        return;

    const vtkIdType nPts = grid->GetNumberOfPoints();
    for (vtkIdType pid = 0; pid < nPts; ++pid)
    {
        const int nodeId = nodeIdArr->GetValue(pid);
        const int pointId = static_cast<int>(pid);
        pick.nodeIdToPointId.insert(nodeId, pointId);
        pick.pointIdToNodeId.insert(pointId, nodeId);
    }
}

static void rebuildBoundaryRuntimeIndexFromNodeData(
    MshPickMap& pick,
    vtkUnstructuredGrid* grid,
    const QHash<int, QVector<int>>& pointPhysToNodeIds,
    const QHash<int, QVector<int>>& edgePhysToNodeIds,
    const QHash<int, QVector<int>>& facePhysToNodeIds,
    const QHash<int, QVector<QVector<int>>>& edgeNodeElems,
    const QHash<int, QVector<QVector<int>>>& faceNodeElems)
{
    if (!grid)
        return;

    const QHash<int, int> nodeToPoint = buildNodeIdToPointId(grid);

    for (auto it = pointPhysToNodeIds.constBegin(); it != pointPhysToNodeIds.constEnd(); ++it)
        setPhysicalBoundaryPointIds(pick, it.key(), 0, nodeIdsToPointIds(it.value(), nodeToPoint), QStringLiteral("ProjectRestoreLegacy"));

    for (auto it = edgePhysToNodeIds.constBegin(); it != edgePhysToNodeIds.constEnd(); ++it)
        setPhysicalBoundaryPointIds(pick, it.key(), 1, nodeIdsToPointIds(it.value(), nodeToPoint), QStringLiteral("ProjectRestoreLegacy"));

    for (auto it = facePhysToNodeIds.constBegin(); it != facePhysToNodeIds.constEnd(); ++it)
        setPhysicalBoundaryPointIds(pick, it.key(), 2, nodeIdsToPointIds(it.value(), nodeToPoint), QStringLiteral("ProjectRestoreLegacy"));

    QHash<quint64, qint64> edgeIndex;
    QHash<QByteArray, qint64> faceIndex;

    const vtkIdType nCells = grid->GetNumberOfCells();
    for (vtkIdType cid = 0; cid < nCells; ++cid)
    {
        vtkCell* cell = grid->GetCell(cid);
        if (!cell)
            continue;

        const int nEdges = cell->GetNumberOfEdges();
        for (int ei = 0; ei < nEdges; ++ei)
        {
            vtkCell* edge = cell->GetEdge(ei);
            if (!edge || edge->GetNumberOfPoints() < MIN_EDGE_POINTS)
                continue;
            const int p0 = static_cast<int>(edge->GetPointId(0));
            const int p1 = static_cast<int>(edge->GetPointId(1));
            const quint64 key = packEdgePointIds(p0, p1);
            const qint64 edgeKey = (qint64(cid) << 4) | (qint64(ei) & 0xF);
            if (!edgeIndex.contains(key))
                edgeIndex.insert(key, edgeKey);
        }

        const int nFaces = cell->GetNumberOfFaces();
        for (int fi = 0; fi < nFaces; ++fi)
        {
            vtkCell* face = cell->GetFace(fi);
            if (!face || face->GetNumberOfPoints() < MIN_FACE_POINTS)
                continue;
            QVector<int> pids;
            pids.reserve(face->GetNumberOfPoints());
            for (vtkIdType i = 0; i < face->GetNumberOfPoints(); ++i)
                pids.push_back(static_cast<int>(face->GetPointId(i)));
            const QByteArray key = packFacePointIds(pids);
            const qint64 faceKey = (qint64(cid) << 4) | (qint64(fi) & 0xF);
            if (!faceIndex.contains(key))
                faceIndex.insert(key, faceKey);
        }
    }

    for (auto it = edgeNodeElems.constBegin(); it != edgeNodeElems.constEnd(); ++it)
    {
        const int physId = it.key();
        for (const QVector<int>& nodePair : it.value())
        {
            if (nodePair.size() < 2)
                continue;
            const int p0 = nodeToPoint.value(nodePair[0], -1);
            const int p1 = nodeToPoint.value(nodePair[1], -1);
            if (p0 < 0 || p1 < 0)
                continue;
            const quint64 key = packEdgePointIds(p0, p1);
            if (edgeIndex.contains(key))
                appendPhysicalBoundaryEdgeKey(pick, physId, edgeIndex.value(key), QStringLiteral("ProjectRestoreLegacy"));
        }
    }

    for (auto it = faceNodeElems.constBegin(); it != faceNodeElems.constEnd(); ++it)
    {
        const int physId = it.key();
        for (const QVector<int>& faceNodes : it.value())
        {
            QVector<int> pids;
            pids.reserve(faceNodes.size());
            for (int nodeId : faceNodes)
            {
                const int pid = nodeToPoint.value(nodeId, -1);
                if (pid >= 0)
                    pids.push_back(pid);
            }
            if (pids.size() < 3)
                continue;
            const QByteArray key = packFacePointIds(pids);
            if (faceIndex.contains(key))
                appendPhysicalBoundaryFaceKey(pick, physId, faceIndex.value(key), QStringLiteral("ProjectRestoreLegacy"));
        }
    }

    normalizeMeshTagSets(pick);
}

static bool saveMeshMapJson(const QString& filePath,
                            const MshPickMap& pick,
                            vtkUnstructuredGrid* grid,
                            QString* errMsg)
{
    QJsonObject root;
    Q_UNUSED(grid);

    root["mesh_dim"] = pick.meshDim;
    root["material_group_category"] = pick.materialGroupCategory;
    root["excavation_group_category"] = pick.excavationGroupCategory;
    root["phys_id_to_name"] = intStringHashToJson(pick.physIdToName);
    root["phys_id_to_dim"] = intIntHashToJson(pick.physIdToDim);

    QJsonArray tagSets;
    for (auto it = pick.tagSets.constBegin(); it != pick.tagSets.constEnd(); ++it)
    {
        const MeshTagSet& set = it.value();

        // MATYPE/LAYER 是由网格 CellData 派生出来的运行期分组，
        // 大网格时 cellIds 会非常大，保存它们只会造成工程文件膨胀。
        // 加载工程后会通过 syncPickMapGroupingFromGrid() 从网格重新构建。
        if (isGridCellDataDomainTag(set.key))
            continue;

        tagSets.append(meshTagSetToJson(set));
    }

    root["tag_sets"] = tagSets;
    QJsonObject displayNames;
    for (auto it = pick.groupDisplayNames.constBegin(); it != pick.groupDisplayNames.constEnd(); ++it)
        displayNames.insert(it.key(), it.value());
    root["group_display_names"] = displayNames;

    QFile file(filePath);

    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        if (errMsg)
            *errMsg = QString("无法写入网格映射文件：%1").arg(filePath);
        return false;
    }

    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    file.close();

    return true;
}

static bool loadMeshMapJson(const QString& filePath,
                            vtkUnstructuredGrid* grid,
                            QString* errMsg)
{
    QFile file(filePath);

    if (!file.open(QIODevice::ReadOnly))
    {
        if (errMsg)
            *errMsg = QString("无法读取网格映射文件：%1").arg(filePath);
        return false;
    }

    const QByteArray bytes = file.readAll();
    file.close();

    const QJsonObject root = QJsonDocument::fromJson(bytes).object();

    MshPickMap pick;

    pick.meshDim = root.value("mesh_dim").toInt();
    const QJsonObject displayNames = root.value("group_display_names").toObject();
    for (auto it = displayNames.begin(); it != displayNames.end(); ++it)
        pick.groupDisplayNames.insert(it.key(), it.value().toString());
    pick.materialGroupCategory =
        root.value("material_group_category").toString().trimmed();
    pick.excavationGroupCategory =
        root.value("excavation_group_category").toString().trimmed();
    pick.physIdToName =
        jsonToIntStringHash(root.value("phys_id_to_name").toObject());
    pick.physIdToDim =
        jsonToIntIntHash(root.value("phys_id_to_dim").toObject());

    const QJsonArray tagSets = root.value("tag_sets").toArray();

    for (const auto& v : tagSets)
    {
        const MeshTagSet set = jsonToMeshTagSet(v.toObject());
        pick.tagSets.insert(set.key, set);
    }

    // cellIdToDomainTags 不再落盘。它是运行期反查缓存，
    // MeshModuleController::loadProject() 成功加载 mesh_map.json 后会调用
    // syncPickMapGroupingFromGrid() 根据 tagSets + 网格 CellData 统一重建。
    pick.cellIdToDomainTags.clear();

    rebuildCurrentNodeMaps(pick, grid);

    const QHash<int, QVector<int>> pointPhysToNodeIds =
        jsonToIntVectorHash(root.value("bnd_point_phys_to_node_ids").toObject());

    const QHash<int, QVector<int>> edgePhysToNodeIds =
        jsonToIntVectorHash(root.value("bnd_edge_phys_to_node_ids").toObject());

    const QHash<int, QVector<int>> facePhysToNodeIds =
        jsonToIntVectorHash(root.value("bnd_face_phys_to_node_ids").toObject());

    const QHash<int, QVector<QVector<int>>> edgeNodeElems =
        jsonToNestedIntVectorHash(root.value("bnd_edge_phys_to_node_elems").toObject());

    const QHash<int, QVector<QVector<int>>> faceNodeElems =
        jsonToNestedIntVectorHash(root.value("bnd_face_phys_to_node_elems").toObject());

    rebuildBoundaryRuntimeIndexFromNodeData(pick,
                                            grid,
                                            pointPhysToNodeIds,
                                            edgePhysToNodeIds,
                                            facePhysToNodeIds,
                                            edgeNodeElems,
                                            faceNodeElems);

    normalizeMeshTagSets(pick);
    MeshGridIO::instance().setMeshMap(pick);

    return true;
}

ProjectNode* findDirectChildByKind(ProjectNode* parent, const QString& kind)
{
    if (!parent)
        return nullptr;
    for (int i = 0; i < parent->childCount(); ++i)
    {
        auto* child = dynamic_cast<ProjectNode*>(parent->child(i));
        if (!child)
            continue;
        if (child->data(0, ProjectNode::RoleKind).toString() == kind)
            return child;
    }
    return nullptr;
}

void rebuildOriginalIdArrays(vtkUnstructuredGrid* sourceGrid, vtkPolyData* poly)
{
    if (!poly)
        return;

    auto pointIds = vtkSmartPointer<vtkIdTypeArray>::New();
    pointIds->SetName("vtkOriginalPointIds");
    pointIds->SetNumberOfComponents(1);
    pointIds->SetNumberOfTuples(poly->GetNumberOfPoints());

    auto cellIds = vtkSmartPointer<vtkIdTypeArray>::New();
    cellIds->SetName("vtkOriginalCellIds");
    cellIds->SetNumberOfComponents(1);
    cellIds->SetNumberOfTuples(poly->GetNumberOfCells());

    for (vtkIdType pid = 0; pid < poly->GetNumberOfPoints(); ++pid)
        pointIds->SetValue(pid, -1);
    for (vtkIdType cid = 0; cid < poly->GetNumberOfCells(); ++cid)
        cellIds->SetValue(cid, -1);

    if (sourceGrid)
    {
        auto pointLocator = vtkSmartPointer<vtkPointLocator>::New();
        pointLocator->SetDataSet(sourceGrid);
        pointLocator->BuildLocator();

        const vtkIdType nSourceCells = sourceGrid->GetNumberOfCells();
        vtkDataArray* sourceOrigCellIds =
            sourceGrid->GetCellData() ? sourceGrid->GetCellData()->GetArray("vtkOriginalCellIds") : nullptr;
        QVector<QSet<vtkIdType>> sourceCellPointSets((int)nSourceCells);
        QVector<char> sourceCellPointSetBuilt((int)nSourceCells, 0);
        auto containsAllMappedPoints = [&](vtkIdType sourceCellId,
                                           const QVector<vtkIdType>& mappedPointIds) -> bool
        {
            if (sourceCellId < 0 || sourceCellId >= nSourceCells || mappedPointIds.isEmpty())
                return false;

            if (!sourceCellPointSetBuilt[(int)sourceCellId])
            {
                vtkCell* sourceCell = sourceGrid->GetCell(sourceCellId);
                if (!sourceCell)
                    return false;
                vtkIdList* sourceIds = sourceCell->GetPointIds();
                if (!sourceIds || sourceIds->GetNumberOfIds() <= 0)
                    return false;
                auto& pointSet = sourceCellPointSets[(int)sourceCellId];
                for (vtkIdType i = 0; i < sourceIds->GetNumberOfIds(); ++i)
                    pointSet.insert(sourceIds->GetId(i));
                sourceCellPointSetBuilt[(int)sourceCellId] = 1;
            }

            const auto& pointSet = sourceCellPointSets[(int)sourceCellId];
            for (vtkIdType pid : mappedPointIds)
            {
                if (!pointSet.contains(pid))
                    return false;
            }
            return true;
        };

               // 通过几何位置回映射到 sourceGrid 点 id。
        for (vtkIdType pid = 0; pid < poly->GetNumberOfPoints(); ++pid)
        {
            double p[3] = {0.0, 0.0, 0.0};
            poly->GetPoint(pid, p);
            const vtkIdType sid = pointLocator->FindClosestPoint(p);
            if (sid >= 0)
                pointIds->SetValue(pid, sid);
        }

        auto candidateCells = vtkSmartPointer<vtkIdList>::New();
        for (vtkIdType cid = 0; cid < poly->GetNumberOfCells(); ++cid)
        {
            vtkCell* surfCell = poly->GetCell(cid);
            if (!surfCell)
                continue;

            vtkIdList* surfPids = surfCell->GetPointIds();
            if (!surfPids || surfPids->GetNumberOfIds() <= 0)
                continue;

            QVector<vtkIdType> mappedPointIds;
            mappedPointIds.reserve((int)surfPids->GetNumberOfIds());
            QSet<vtkIdType> mappedPointSet;
            bool hasInvalidPid = false;
            for (vtkIdType i = 0; i < surfPids->GetNumberOfIds(); ++i)
            {
                const vtkIdType spid = surfPids->GetId(i);
                if (spid < 0 || spid >= pointIds->GetNumberOfValues())
                {
                    hasInvalidPid = true;
                    break;
                }
                const vtkIdType mappedPid = pointIds->GetValue(spid);
                if (mappedPid < 0)
                {
                    hasInvalidPid = true;
                    break;
                }
                if (!mappedPointSet.contains(mappedPid))
                {
                    mappedPointSet.insert(mappedPid);
                    mappedPointIds.push_back(mappedPid);
                }
            }
            if (hasInvalidPid || mappedPointIds.isEmpty())
                continue;

            sourceGrid->GetPointCells(mappedPointIds.front(), candidateCells);
            vtkIdType bestSourceCellId = -1;
            int bestDim = -1;

            for (vtkIdType i = 0; i < candidateCells->GetNumberOfIds(); ++i)
            {
                const vtkIdType sourceCellId = candidateCells->GetId(i);
                vtkCell* sourceCell = sourceGrid->GetCell(sourceCellId);
                if (!sourceCell)
                    continue;
                if (!containsAllMappedPoints(sourceCellId, mappedPointIds))
                    continue;

                const int dim = sourceCell->GetCellDimension();
                // 优先匹配体单元，其次面单元。
                if (dim > bestDim)
                {
                    bestDim = dim;
                    bestSourceCellId = sourceCellId;
                }
            }

            if (bestSourceCellId >= 0)
            {
                vtkIdType finalCellId = bestSourceCellId;
                if (sourceOrigCellIds && bestSourceCellId < sourceOrigCellIds->GetNumberOfTuples())
                    finalCellId = static_cast<vtkIdType>(sourceOrigCellIds->GetTuple1(bestSourceCellId));
                cellIds->SetValue(cid, finalCellId);
            }
        }
    }

    if (auto* pd = poly->GetPointData())
    {
        pd->RemoveArray("vtkOriginalPointIds");
        pd->AddArray(pointIds);
    }
    if (auto* cd = poly->GetCellData())
    {
        cd->RemoveArray("vtkOriginalCellIds");
        cd->AddArray(cellIds);
    }
}



static const QString kMeshDisplayFilterCustomKey = QStringLiteral("mesh.display.filter.custom");
static const QString kMeshDisplayFilterKetypeKey = QStringLiteral("mesh.display.filter.ketype.checked");
static const QString kMeshDisplayFilterMatypeKey = QStringLiteral("mesh.display.filter.matype.checked");
static const QString kMeshDisplayFilterLayerKey = QStringLiteral("mesh.display.filter.layer.checked");
static const QString kMeshDisplayFilterRegionKey = QStringLiteral("mesh.display.filter.region.checked");
static const QString kMeshDisplayColorModeKey = QStringLiteral("mesh.display.color.mode");

static void meshDisplayPreferSourceColors(vtkUnstructuredGrid* grid)
{
    auto* cellData = grid ? grid->GetCellData() : nullptr;
    auto* matype = cellData ? cellData->GetArray("MATYPE") : nullptr;
    int colorMode = (matype && matype->GetNumberOfTuples() > 0) ? 2 : 0;
    auto* regions = cellData ? cellData->GetArray("src_part") : nullptr;
    QSet<int> regionIds;
    for (vtkIdType cid = 0; regions && cid < regions->GetNumberOfTuples(); ++cid)
    {
        const int id = static_cast<int>(regions->GetTuple1(cid));
        if (id > 0)
            regionIds.insert(id);
        if (regionIds.size() > 1)
        {
            colorMode = static_cast<int>(GridEditingWidget::ColorMode::Region);
            break;
        }
    }
    PublicVariables::instance().set(kMeshDisplayColorModeKey, colorMode,
                                    QStringLiteral("project_dir"));
    PublicVariables::instance().set(kMeshDisplayFilterCustomKey, false,
                                    QStringLiteral("project_dir"));
}

static QSet<int> meshDisplayIntSetFromString(const QString& text)
{
    QSet<int> values;
    const QStringList parts = text.split(',', Qt::SkipEmptyParts);
    for (const QString& part : parts)
    {
        bool ok = false;
        const int value = part.trimmed().toInt(&ok);
        if (ok)
            values.insert(value);
    }
    return values;
}

static int meshDisplayTuple1i(vtkDataArray* array, vtkIdType tupleId, int defaultValue = 0)
{
    if (!array || tupleId < 0 || tupleId >= array->GetNumberOfTuples())
        return defaultValue;
    return static_cast<int>(array->GetTuple1(tupleId));
}

static int meshDisplayDimFromKetype(int ketype)
{
    switch (ketype)
    {
    case 1:
    case 2:
    case 10:
    case 21:
        return 1;
    case 3:
    case 4:
    case 5:
    case 11:
    case 12:
    case 29:
    case 31:
    case 32:
    case 33:
    case 34:
        return 2;
    case 6:
    case 9:
    case 13:
    case 17:
    case 35:
    case 36:
    case 37:
    case 38:
    case 39:
        return 3;
    default:
        return 0;
    }
}

static int meshDisplayCellDimensionFast(vtkUnstructuredGrid* grid, vtkIdType cellId)
{
    if (!grid || cellId < 0 || cellId >= grid->GetNumberOfCells())
        return 0;

    auto* cd = grid->GetCellData();
    if (cd)
    {
        if (auto* dimArr = cd->GetArray("CELL_DIM"))
        {
            const int dim = meshDisplayTuple1i(dimArr, cellId, 0);
            if (dim > 0)
                return dim;
        }

        if (auto* ketypeArr = cd->GetArray("KETYPE"))
        {
            const int dim = meshDisplayDimFromKetype(meshDisplayTuple1i(ketypeArr, cellId, 0));
            if (dim > 0)
                return dim;
        }
    }

    vtkCell* cell = grid->GetCell(cellId);
    return cell ? cell->GetCellDimension() : 0;
}

static QColor meshDisplayCategoricalColor(int value)
{
    if (value == 0)
        return QColor::fromRgbF(0.78, 0.78, 0.78);

    const int hue = ((value * 47) % 360 + 360) % 360;
    return QColor::fromHsv(hue, 170, 220);
}

static vtkSmartPointer<vtkUnstructuredGrid> meshDisplayCopyWithFullCellIds(vtkUnstructuredGrid* grid)
{
    auto out = vtkSmartPointer<vtkUnstructuredGrid>::New();
    if (!grid)
        return out;

    out->ShallowCopy(grid);

    auto fullIds = vtkSmartPointer<vtkIdTypeArray>::New();
    fullIds->SetName("vtkOriginalCellIds");
    fullIds->SetNumberOfComponents(1);
    fullIds->SetNumberOfTuples(grid->GetNumberOfCells());

    auto* oldCellIds = grid->GetCellData() ? grid->GetCellData()->GetArray("vtkOriginalCellIds") : nullptr;
    for (vtkIdType cid = 0; cid < grid->GetNumberOfCells(); ++cid)
    {
        vtkIdType oid = cid;
        if (oldCellIds && cid < oldCellIds->GetNumberOfTuples())
            oid = static_cast<vtkIdType>(oldCellIds->GetTuple1(cid));
        fullIds->SetValue(cid, oid);
    }

    if (auto* cd = out->GetCellData())
    {
        cd->RemoveArray("vtkOriginalCellIds");
        cd->AddArray(fullIds);
    }

    auto fullPointIds = vtkSmartPointer<vtkIdTypeArray>::New();
    fullPointIds->SetName("vtkOriginalPointIds");
    fullPointIds->SetNumberOfComponents(1);
    fullPointIds->SetNumberOfTuples(grid->GetNumberOfPoints());

    auto* oldPointIds = grid->GetPointData() ? grid->GetPointData()->GetArray("vtkOriginalPointIds") : nullptr;
    for (vtkIdType pid = 0; pid < grid->GetNumberOfPoints(); ++pid)
    {
        vtkIdType oid = pid;
        if (oldPointIds && pid < oldPointIds->GetNumberOfTuples())
            oid = static_cast<vtkIdType>(oldPointIds->GetTuple1(pid));
        fullPointIds->SetValue(pid, oid);
    }

    if (auto* pd = out->GetPointData())
    {
        pd->RemoveArray("vtkOriginalPointIds");
        pd->AddArray(fullPointIds);
    }

    return out;
}

static vtkSmartPointer<vtkUnstructuredGrid> meshDisplayExtractCells(
    vtkUnstructuredGrid* fullGrid, const vtkSmartPointer<vtkIdList>& ids)
{
    auto out = vtkSmartPointer<vtkUnstructuredGrid>::New();
    if (!fullGrid || !ids || ids->GetNumberOfIds() <= 0)
        return out;

    auto source = meshDisplayCopyWithFullCellIds(fullGrid);

    auto ex = vtkSmartPointer<vtkExtractCells>::New();
    ex->SetInputData(source);
    ex->SetCellList(ids);
    ex->Update();

    out->ShallowCopy(ex->GetOutput());
    return out;
}


static QByteArray meshDisplayFaceKey(vtkIdList* pointIds)
{
    QVector<vtkIdType> ids;
    if (pointIds)
    {
        ids.reserve(static_cast<int>(pointIds->GetNumberOfIds()));
        for (vtkIdType i = 0; i < pointIds->GetNumberOfIds(); ++i)
            ids.push_back(pointIds->GetId(i));
    }

    std::sort(ids.begin(), ids.end());

    QByteArray key;
    key.reserve(ids.size() * int(sizeof(vtkIdType)));
    for (vtkIdType id : ids)
        key.append(reinterpret_cast<const char*>(&id), int(sizeof(vtkIdType)));
    return key;
}

static vtkIdType meshDisplayOriginalCellId(vtkDataSet* grid, vtkIdType cellId)
{
    if (!grid || cellId < 0 || cellId >= grid->GetNumberOfCells())
        return cellId;

    auto* arr = grid->GetCellData() ? grid->GetCellData()->GetArray("vtkOriginalCellIds") : nullptr;
    if (arr && cellId < arr->GetNumberOfTuples())
        return static_cast<vtkIdType>(arr->GetTuple1(cellId));
    return cellId;
}

static int meshDisplayDomainValue(vtkDataSet* grid, vtkIdType cellId, const char* arrayName)
{
    auto* arr = (grid && grid->GetCellData()) ? grid->GetCellData()->GetArray(arrayName) : nullptr;
    if (!arr || cellId < 0 || cellId >= arr->GetNumberOfTuples())
        return 0;
    return static_cast<int>(arr->GetTuple1(cellId));
}

struct MeshDisplayFaceRef
{
    vtkIdType cellId = -1;          // cell id in current display source grid
    vtkIdType originalCellId = -1;  // cell id in m_importedGrid
    int originalFaceId = 15;        // 15 表示 2D 单元整体面
    int matype = 0;
    int layer = 0;
    int vtkCellType = VTK_EMPTY_CELL;
    QVector<vtkIdType> pointIds;    // 保留当前面的完整点序，包括高阶中间节点
};

// 高阶单元显示代理：vtkPolyData 的 polygon 只能按边界点顺序连线，
// 不能把高阶面上的中间节点也当作多边形顶点，否则会出现斜线/蜘蛛网。
// 这里仅取角点用于显示；真实网格、KETYPE、node_id、拾取映射仍保留在原 grid 中。
static QVector<vtkIdType> meshDisplayFaceCornerPointIds(vtkCell* face)
{
    QVector<vtkIdType> ids;
    if (!face)
        return ids;

    vtkIdList* pids = face->GetPointIds();
    if (!pids)
        return ids;

    const int pointCount = static_cast<int>(pids->GetNumberOfIds());
    if (pointCount < 3)
        return ids;

    int cornerCount = pointCount;

    switch (face->GetCellType())
    {
    case VTK_TRIANGLE:
    case VTK_QUADRATIC_TRIANGLE:
    case VTK_BIQUADRATIC_TRIANGLE:
        cornerCount = 3;
        break;

    case VTK_QUAD:
    case VTK_PIXEL:
    case VTK_QUADRATIC_QUAD:
    case VTK_QUADRATIC_LINEAR_QUAD:
    case VTK_BIQUADRATIC_QUAD:
        cornerCount = 4;
        break;

    default:
        // 常见兜底：
        // 6 节点面多为二次三角形；8/9 节点面多为二次四边形。
        if (pointCount == 6)
            cornerCount = 3;
        else if (pointCount == 8 || pointCount == 9)
            cornerCount = 4;
        else
        {
            const int edgeCount = face->GetNumberOfEdges();
            if (edgeCount == 3 || edgeCount == 4)
                cornerCount = edgeCount;
        }
        break;
    }

    cornerCount = std::min(cornerCount, pointCount);
    ids.reserve(cornerCount);

    for (int i = 0; i < cornerCount; ++i)
        ids.push_back(pids->GetId(i));

    return ids;
}


static QVector<QVector<vtkIdType>> meshDisplayFaceDisplayPolys(const MeshDisplayFaceRef& face)
{
    QVector<QVector<vtkIdType>> polys;
    const QVector<vtkIdType>& p = face.pointIds;
    const int n = p.size();

    if (n < 3)
        return polys;

    auto addTri = [&](int a, int b, int c)
    {
        if (a < n && b < n && c < n)
            polys.push_back(QVector<vtkIdType>{p[a], p[b], p[c]});
    };

    auto addQuad = [&](int a, int b, int c, int d)
    {
        if (a < n && b < n && c < n && d < n)
            polys.push_back(QVector<vtkIdType>{p[a], p[b], p[c], p[d]});
    };

    // 二次三角形：VTK_QUADRATIC_TRIANGLE = 0,1,2 为角点，3/4/5 为边中点。
    // 拆成 4 个一阶三角形，这样边中点之间的线也能显示出来。
    if (face.vtkCellType == VTK_QUADRATIC_TRIANGLE ||
        face.vtkCellType == VTK_BIQUADRATIC_TRIANGLE ||
        n == 6)
    {
        addTri(0, 3, 5);
        addTri(3, 1, 4);
        addTri(5, 4, 2);
        addTri(3, 4, 5);
        return polys;
    }

    // 二次四边形：VTK_QUADRATIC_QUAD = 0~3 角点，4~7 边中点。
    // 8 节点没有中心点，用 4 个角三角形 + 1 个中间四边形显示内部网格线。
    if (face.vtkCellType == VTK_QUADRATIC_QUAD || n == 8)
    {
        addTri(0, 4, 7);
        addTri(4, 1, 5);
        addTri(5, 2, 6);
        addTri(7, 6, 3);
        addQuad(4, 5, 6, 7);
        return polys;
    }

    // 九节点四边形：8 是中心点，拆成 4 个小四边形，显示效果最干净。
    if (face.vtkCellType == VTK_BIQUADRATIC_QUAD || n == 9)
    {
        addQuad(0, 4, 8, 7);
        addQuad(4, 1, 5, 8);
        addQuad(8, 5, 2, 6);
        addQuad(7, 8, 6, 3);
        return polys;
    }

    // 六节点二次线性四边形：不同求解器顺序不完全一致，保守显示为角点面，避免乱连。
    if (face.vtkCellType == VTK_QUADRATIC_LINEAR_QUAD && n >= 4)
    {
        addQuad(0, 1, 2, 3);
        return polys;
    }

    // 一阶三角/四边形，或者未知面：只用角点显示。
    if (n == 3)
        polys.push_back(p);
    else if (n >= 4)
        addQuad(0, 1, 2, 3);

    return polys;
}

static bool meshDisplayDifferentDomain(const MeshDisplayFaceRef& a,
                                       const MeshDisplayFaceRef& b,
                                       bool hasMatype,
                                       bool hasLayer)
{
    if (hasLayer && a.layer != b.layer)
        return true;
    if (hasMatype && a.matype != b.matype)
        return true;
    return false;
}

static void meshDisplayAddSurfaceFace(vtkPolyData* out,
                                      vtkCellArray* polys,
                                      vtkIdTypeArray* originalCellIds,
                                      vtkIdTypeArray* originalFaceIds,
                                      vtkIdTypeArray* originalFaceKeys,
                                      vtkCellData* sourceCellData,
                                      const MeshDisplayFaceRef& face)
{
    if (!out || !polys || !originalCellIds || !originalFaceIds || !originalFaceKeys || face.pointIds.size() < 3)
        return;

    const QVector<QVector<vtkIdType>> displayPolys = meshDisplayFaceDisplayPolys(face);
    if (displayPolys.isEmpty())
        return;

    for (const QVector<vtkIdType>& polyPts : displayPolys)
    {
        if (polyPts.size() < 3)
            continue;

        const vtkIdType newCellId = polys->GetNumberOfCells();

        // 不创建临时 vtkIdList，避免导入大网格时每个面一次堆分配。
        polys->InsertNextCell(static_cast<vtkIdType>(polyPts.size()));
        for (vtkIdType pid : polyPts)
            polys->InsertCellPoint(pid);

        if (sourceCellData && out->GetCellData())
            out->GetCellData()->CopyData(sourceCellData, face.cellId, newCellId);

        // 一个高阶面可能拆成多个显示小面，但它们都回指到同一个原始单元/原始面。
        originalCellIds->InsertNextValue(face.originalCellId);
        originalFaceIds->InsertNextValue(face.originalFaceId);
        originalFaceKeys->InsertNextValue((static_cast<vtkIdType>(face.originalCellId) << 4) |
                                          (static_cast<vtkIdType>(face.originalFaceId) & 0xF));
    }
}

static bool meshDisplayCellPassesDisplayFilter(vtkUnstructuredGrid* grid,
                                               vtkIdType cellId,
                                               bool customFilter,
                                               const QSet<int>& ketypes,
                                               const QSet<int>& matypes,
                                               const QSet<int>& layers,
                                               const QSet<int>* regions = nullptr)
{
    if (!customFilter)
        return true;
    if (!grid || cellId < 0 || cellId >= grid->GetNumberOfCells())
        return false;

    auto allowed = [](int value, const QSet<int>& values) -> bool
    {
        return values.isEmpty() || values.contains(value);
    };

    // An empty stored region selection means hide all regions. A missing key
    // is a legacy filter, which must continue to include every source region.
    auto* regionArray = grid->GetCellData() ? grid->GetCellData()->GetArray("src_part") : nullptr;
    if (regionArray && regions)
    {
        if (!regions->contains(meshDisplayDomainValue(grid, cellId, "src_part")))
            return false;
    }

    return allowed(meshDisplayDomainValue(grid, cellId, "KETYPE"), ketypes) &&
           allowed(meshDisplayDomainValue(grid, cellId, "MATYPE"), matypes) &&
           allowed(meshDisplayDomainValue(grid, cellId, "LAYER"), layers);
}

static vtkSmartPointer<vtkPolyData> meshDisplayBuildMappedSurface(vtkUnstructuredGrid* grid,
                                                                  bool customFilter = false,
                                                                  const QSet<int>& ketypes = QSet<int>(),
                                                                  const QSet<int>& matypes = QSet<int>(),
                                                                  const QSet<int>& layers = QSet<int>())
{
    auto out = vtkSmartPointer<vtkPolyData>::New();
    if (!grid || grid->GetNumberOfCells() <= 0 || !grid->GetPoints())
        return out;

    const QSet<int> regions = meshDisplayIntSetFromString(
        PublicVariables::instance().get(kMeshDisplayFilterRegionKey).toString());
    const QSet<int>* regionFilter = PublicVariables::instance().contains(kMeshDisplayFilterRegionKey)
        ? &regions : nullptr;

           // 无论默认显示还是自定义筛选，都保留原有完整 mapped-surface 语义：
           // 外表面 + MATYPE/LAYER 内部分界面都生成。性能通过缓存解决，不能通过删显示面解决。
           // 关键：直接复用显示源网格的点表。这样 PickManager 看到的 surface pid 与原网格 pid 一致，
           // 不再通过 vtkDataSetSurfaceFilter + 最近点回猜，避免点选/框选“乱窜”。
    out->SetPoints(grid->GetPoints());

    auto polys = vtkSmartPointer<vtkCellArray>::New();
    out->SetPolys(polys);

    vtkCellData* srcCellData = grid->GetCellData();
    if (srcCellData)
        out->GetCellData()->CopyAllocate(srcCellData, grid->GetNumberOfCells() * 6);

    auto originalPointIds = vtkSmartPointer<vtkIdTypeArray>::New();
    originalPointIds->SetName("vtkOriginalPointIds");
    originalPointIds->SetNumberOfComponents(1);
    originalPointIds->SetNumberOfTuples(grid->GetNumberOfPoints());

    auto* sourceOriginalPointIds = grid->GetPointData()
                                       ? grid->GetPointData()->GetArray("vtkOriginalPointIds")
                                       : nullptr;
    for (vtkIdType pid = 0; pid < grid->GetNumberOfPoints(); ++pid)
    {
        vtkIdType oid = pid;
        if (sourceOriginalPointIds && pid < sourceOriginalPointIds->GetNumberOfTuples())
            oid = static_cast<vtkIdType>(sourceOriginalPointIds->GetTuple1(pid));
        originalPointIds->SetValue(pid, oid);
    }

    auto originalCellIds = vtkSmartPointer<vtkIdTypeArray>::New();
    originalCellIds->SetName("vtkOriginalCellIds");
    originalCellIds->SetNumberOfComponents(1);

    auto originalFaceIds = vtkSmartPointer<vtkIdTypeArray>::New();
    originalFaceIds->SetName("vtkOriginalFaceIds");
    originalFaceIds->SetNumberOfComponents(1);

    auto originalFaceKeys = vtkSmartPointer<vtkIdTypeArray>::New();
    originalFaceKeys->SetName("vtkOriginalFaceKeys");
    originalFaceKeys->SetNumberOfComponents(1);

    const bool hasMatype = srcCellData && srcCellData->GetArray("MATYPE");
    const bool hasLayer = srcCellData && srcCellData->GetArray("LAYER");

    QHash<QByteArray, QVector<MeshDisplayFaceRef>> faceMap;
    faceMap.reserve(static_cast<int>(grid->GetNumberOfCells() * 3));

    const vtkIdType nCells = grid->GetNumberOfCells();
    for (vtkIdType cid = 0; cid < nCells; ++cid)
    {
        if (!meshDisplayCellPassesDisplayFilter(grid, cid, customFilter, ketypes, matypes, layers, regionFilter))
            continue;

        const int dim = meshDisplayCellDimensionFast(grid, cid);
        vtkCell* cell = grid->GetCell(cid);
        if (!cell)
            continue;

        MeshDisplayFaceRef ref;
        ref.cellId = cid;
        ref.originalCellId = meshDisplayOriginalCellId(grid, cid);
        ref.matype = meshDisplayDomainValue(grid, cid, "MATYPE");
        ref.layer = meshDisplayDomainValue(grid, cid, "LAYER");

        if (dim == 2)
        {
            ref.originalFaceId = 15;
            ref.vtkCellType = cell->GetCellType();

            vtkIdList* pids = cell->GetPointIds();
            if (!pids || pids->GetNumberOfIds() < 3)
                continue;

            ref.pointIds.reserve(static_cast<int>(pids->GetNumberOfIds()));
            for (vtkIdType i = 0; i < pids->GetNumberOfIds(); ++i)
                ref.pointIds.push_back(pids->GetId(i));

            meshDisplayAddSurfaceFace(out, polys, originalCellIds, originalFaceIds, originalFaceKeys, srcCellData, ref);
            continue;
        }

        if (dim != 3)
            continue;

        const int nFaces = cell->GetNumberOfFaces();
        for (int fi = 0; fi < nFaces; ++fi)
        {
            vtkCell* face = cell->GetFace(fi);
            if (!face)
                continue;

            vtkIdList* pids = face->GetPointIds();
            if (!pids || pids->GetNumberOfIds() < 3)
                continue;

            MeshDisplayFaceRef one = ref;
            one.originalFaceId = fi;
            one.vtkCellType = face->GetCellType();
            one.pointIds.reserve(static_cast<int>(pids->GetNumberOfIds()));
            for (vtkIdType i = 0; i < pids->GetNumberOfIds(); ++i)
                one.pointIds.push_back(pids->GetId(i));

            if (one.pointIds.size() < 3)
                continue;

            // faceMap 使用完整高阶面点集作为 key，用于正确识别相邻单元共享面；
            // 写入 vtkPolyData 时再拆成若干一阶显示小面，避免高阶点乱连。
            faceMap[meshDisplayFaceKey(pids)].push_back(one);
        }
    }

           // 外表面：只有一个 owner；内部材料/分层交界面：两个 owner 但 MATYPE/LAYER 不同，也需要显示和可拾取。
    for (auto it = faceMap.constBegin(); it != faceMap.constEnd(); ++it)
    {
        const QVector<MeshDisplayFaceRef>& owners = it.value();
        if (owners.isEmpty())
            continue;

        if (owners.size() == 1)
        {
            meshDisplayAddSurfaceFace(out, polys, originalCellIds, originalFaceIds, originalFaceKeys, srcCellData, owners[0]);
            continue;
        }

        for (int i = 0; i < owners.size(); ++i)
        {
            bool visibleInterface = false;
            for (int j = 0; j < owners.size(); ++j)
            {
                if (i == j)
                    continue;
                if (meshDisplayDifferentDomain(owners[i], owners[j], hasMatype, hasLayer))
                {
                    visibleInterface = true;
                    break;
                }
            }

            if (visibleInterface)
                meshDisplayAddSurfaceFace(out, polys, originalCellIds, originalFaceIds, originalFaceKeys, srcCellData, owners[i]);
        }
    }

    if (out->GetPointData())
    {
        out->GetPointData()->RemoveArray("vtkOriginalPointIds");
        out->GetPointData()->AddArray(originalPointIds);
    }

    if (out->GetCellData())
    {
        out->GetCellData()->RemoveArray("vtkOriginalCellIds");
        out->GetCellData()->RemoveArray("vtkOriginalFaceIds");
        out->GetCellData()->RemoveArray("vtkOriginalFaceKeys");
        out->GetCellData()->AddArray(originalCellIds);
        out->GetCellData()->AddArray(originalFaceIds);
        out->GetCellData()->AddArray(originalFaceKeys);
    }

    out->Modified();
    return out;
}

static void meshDisplaySetMapperInput(vtkMapper* mapper, vtkDataSet* dataSet)
{
    if (!mapper || !dataSet)
        return;

    if (auto* polyMapper = vtkPolyDataMapper::SafeDownCast(mapper))
    {
        if (auto* poly = vtkPolyData::SafeDownCast(dataSet))
            polyMapper->SetInputData(poly);
        return;
    }

    if (auto* dataSetMapper = vtkDataSetMapper::SafeDownCast(mapper))
        dataSetMapper->SetInputData(dataSet);
}

static bool meshDisplayCellMatchesFilter(vtkUnstructuredGrid* grid,
                                         vtkIdType cellId,
                                         bool customFilter,
                                         const QSet<int>& ketypes,
                                         const QSet<int>& matypes,
                                         const QSet<int>& layers)
{
    return meshDisplayCellPassesDisplayFilter(grid, cellId, customFilter, ketypes, matypes, layers);
}

static QSet<int> meshDisplayCollectCellInts(vtkDataSet* dataSet, const char* arrayName)
{
    QSet<int> values;
    auto* array = dataSet && dataSet->GetCellData() ? dataSet->GetCellData()->GetArray(arrayName) : nullptr;
    if (!array)
        return values;

    for (vtkIdType i = 0; i < array->GetNumberOfTuples(); ++i)
        values.insert(static_cast<int>(array->GetTuple1(i)));
    return values;
}

static vtkSmartPointer<vtkLookupTable> meshDisplayBuildLookupTable(const QSet<int>& values)
{
    if (values.isEmpty())
        return nullptr;

    int minValue = std::numeric_limits<int>::max();
    int maxValue = std::numeric_limits<int>::min();
    for (int value : values)
    {
        minValue = qMin(minValue, value);
        maxValue = qMax(maxValue, value);
    }
    if (minValue > maxValue)
        return nullptr;

    const int tableSize = maxValue - minValue + 1;
    auto lut = vtkSmartPointer<vtkLookupTable>::New();
    lut->SetNumberOfTableValues(tableSize);
    lut->SetTableRange(static_cast<double>(minValue), static_cast<double>(maxValue));
    lut->Build();

    for (int i = 0; i < tableSize; ++i)
    {
        const int value = minValue + i;
        const QColor color = values.contains(value) ? meshDisplayCategoricalColor(value)
                                                    : QColor::fromRgbF(0.78, 0.78, 0.78);
        lut->SetTableValue(i, color.redF(), color.greenF(), color.blueF(), 1.0);
    }

    return lut;
}

static void meshDisplayApplyScalarColor(vtkMapper* mapper,
                                        vtkDataSet* dataSet,
                                        const char* arrayName,
                                        vtkLookupTable* lut)
{
    if (!mapper || !dataSet || !arrayName || !lut)
        return;

    auto* cd = dataSet->GetCellData();
    if (!cd || !cd->GetArray(arrayName))
    {
        mapper->ScalarVisibilityOff();
        return;
    }

    mapper->SetScalarModeToUseCellFieldData();
    mapper->SelectColorArray(arrayName);
    mapper->SetLookupTable(lut);
    mapper->UseLookupTableScalarRangeOn();
    mapper->ScalarVisibilityOn();
}

static vtkSmartPointer<vtkUnstructuredGrid> meshDisplayBuildLineGrid(vtkUnstructuredGrid* fullGrid,
                                                                     bool customFilter,
                                                                     const QSet<int>& ketypes,
                                                                     const QSet<int>& matypes,
                                                                     const QSet<int>& layers)
{
    auto lineIds = vtkSmartPointer<vtkIdList>::New();
    if (!fullGrid)
        return vtkSmartPointer<vtkUnstructuredGrid>::New();

    const QSet<int> regions = meshDisplayIntSetFromString(
        PublicVariables::instance().get(kMeshDisplayFilterRegionKey).toString());
    const QSet<int>* regionFilter = PublicVariables::instance().contains(kMeshDisplayFilterRegionKey)
        ? &regions : nullptr;

    const vtkIdType nCells = fullGrid->GetNumberOfCells();
    for (vtkIdType cid = 0; cid < nCells; ++cid)
    {
        if (!meshDisplayCellPassesDisplayFilter(fullGrid, cid, customFilter, ketypes, matypes, layers, regionFilter))
            continue;
        if (meshDisplayCellDimensionFast(fullGrid, cid) == 1)
            lineIds->InsertNextId(cid);
    }

    if (lineIds->GetNumberOfIds() <= 0)
        return vtkSmartPointer<vtkUnstructuredGrid>::New();

    return meshDisplayExtractCells(fullGrid, lineIds);
}

static void meshDisplayApplyGlobalState(vtkUnstructuredGrid* fullGrid,
                                        vtkActor* surfaceActor,
                                        vtkMapper* surfaceMapper,
                                        vtkActor* lineActor,
                                        vtkMapper* lineMapper,
                                        vtkPolyData* cachedSurface = nullptr,
                                        vtkUnstructuredGrid* cachedLineGrid = nullptr)
{
    if (!fullGrid || (!surfaceMapper && !lineMapper))
        return;

    const bool customFilter = PublicVariables::instance().get(kMeshDisplayFilterCustomKey).toBool();

    const QSet<int> ketypes = meshDisplayIntSetFromString(
        PublicVariables::instance().get(kMeshDisplayFilterKetypeKey).toString());
    const QSet<int> matypes = meshDisplayIntSetFromString(
        PublicVariables::instance().get(kMeshDisplayFilterMatypeKey).toString());
    const QSet<int> layers = meshDisplayIntSetFromString(
        PublicVariables::instance().get(kMeshDisplayFilterLayerKey).toString());

    vtkSmartPointer<vtkPolyData> viewSurface;
    vtkSmartPointer<vtkUnstructuredGrid> viewLineGrid;

    // 工程恢复且未启用自定义筛选时，优先直接使用上次保存的完整 mapped-surface 缓存，
    // 跳过最昂贵的“遍历全部三维单元 + 构造共享面哈希”过程；显示语义不做任何降级。
    if (!customFilter && cachedSurface && cachedSurface->GetNumberOfCells() > 0)
    {
        viewSurface = vtkSmartPointer<vtkPolyData>::New();
        viewSurface->ShallowCopy(cachedSurface);
    }
    else
    {
        viewSurface = meshDisplayBuildMappedSurface(
            fullGrid, customFilter, ketypes, matypes, layers);
    }

    if (!customFilter && cachedLineGrid && cachedLineGrid->GetNumberOfCells() > 0)
    {
        viewLineGrid = vtkSmartPointer<vtkUnstructuredGrid>::New();
        viewLineGrid->ShallowCopy(cachedLineGrid);
    }
    else
    {
        viewLineGrid = meshDisplayBuildLineGrid(
            fullGrid, customFilter, ketypes, matypes, layers);
    }

    if (surfaceMapper)
        meshDisplaySetMapperInput(surfaceMapper, viewSurface);
    if (lineMapper)
        meshDisplaySetMapperInput(lineMapper, viewLineGrid);

    if (surfaceActor)
        surfaceActor->SetVisibility(viewSurface->GetNumberOfCells() > 0 ? 1 : 0);
    if (lineActor)
        lineActor->SetVisibility(viewLineGrid->GetNumberOfCells() > 0 ? 1 : 0);

    if (surfaceMapper)
        surfaceMapper->ScalarVisibilityOff();
    if (lineMapper)
        lineMapper->ScalarVisibilityOff();

    const int colorMode = PublicVariables::instance().get(kMeshDisplayColorModeKey).toInt();
    const char* arrayName = nullptr;
    switch (colorMode)
    {
    case 1:
        arrayName = "KETYPE";
        break;
    case 2:
        arrayName = "MATYPE";
        break;
    case 3:
        arrayName = "LAYER";
        break;
    case 4:
        arrayName = "TOPOLOGY_TYPE";
        break;
    case 5:
        arrayName = "src_part";
        break;
    default:
        break;
    }

    if (arrayName)
    {
        QSet<int> values = meshDisplayCollectCellInts(viewSurface, arrayName);
        values.unite(meshDisplayCollectCellInts(viewLineGrid, arrayName));
        vtkSmartPointer<vtkLookupTable> lut = meshDisplayBuildLookupTable(values);
        if (lut)
        {
            meshDisplayApplyScalarColor(surfaceMapper, viewSurface, arrayName, lut);
            meshDisplayApplyScalarColor(lineMapper, viewLineGrid, arrayName, lut);
        }
    }

}

} // namespace
#include <QCoreApplication>

// === 这些头文件你后面真正实现时再加 ===
// #include "MeshImportGridWidget.h"
// #include "MeshExportGridWidget.h"
// #include "MeshGlobalMeshingWidget.h"
// #include "MeshSingleMeshingWidget.h"
// #include "MeshMappedMeshingWidget.h"
// #include "MeshSweepMeshingWidget.h"
// #include "MeshQualityStandardWidget.h"

static void syncPickMapGroupingFromGrid(vtkUnstructuredGrid* grid);

static bool normalizeRodElementLayers(vtkUnstructuredGrid* grid)
{
    if (!grid || !grid->GetCellData())
        return false;

    vtkDataArray* ketypes = grid->GetCellData()->GetArray("KETYPE");
    vtkDataArray* layers = grid->GetCellData()->GetArray("LAYER");
    if (!ketypes || !layers)
        return false;

    bool changed = false;
    const vtkIdType tupleCount = qMin(grid->GetNumberOfCells(),
                                      qMin(ketypes->GetNumberOfTuples(),
                                           layers->GetNumberOfTuples()));
    for (vtkIdType cellId = 0; cellId < tupleCount; ++cellId)
    {
        vtkCell* cell = grid->GetCell(cellId);
        if (!cell || cell->GetCellDimension() != 1 ||
            static_cast<int>(ketypes->GetTuple1(cellId)) != 1)
        {
            continue;
        }

        const int current = static_cast<int>(layers->GetTuple1(cellId));
        const int normalized = -qMax(1, qAbs(current));
        if (current != normalized)
        {
            layers->SetTuple1(cellId, normalized);
            changed = true;
        }
    }

    if (changed)
    {
        layers->Modified();
        grid->Modified();
    }
    return changed;
}

MeshModuleController::MeshModuleController(QObject* parent) : QObject(parent)
{
    ServiceRegistry::instance().registerService<MeshModuleControllerService>(this);
    m_visualWidgetService = ServiceRegistry::instance().getService<VisualWidgetService>();
    initAction();
    initTreeMenu();
    ensureGridDisplayControlPanel();
}

void MeshModuleController::initTreeMenu()
{
    ContextMenuHub::instance().registerProvider(
        QStringLiteral("project_tree.node"), this,
        [this](QMenu* menu, const QVariantMap& ctx)
        {
            if (ctx.value("module").toString()=="Mesh") {
                const QString kind=ctx.value("kind").toString();
                const QString uuid=ctx.value("uuid").toString();
                if(kind=="MeshRoot") {
                    if(!findDirectChildByKind(m_gridNode,"GlobalMeshing"))
                        ContextMenuHub::addAction(menu,this,tr("添加整体剖分"),[this]{onMeshGlobal();});
                } else {
                    int dimension=kind=="GlobalMeshing"?3:
                        kind=="SmeshVolumeSubmesh"?3:
                        kind=="SmeshFaceSubmesh"?2:
                        kind=="SmeshEdgeSubmesh"?1:0;
                    if(kind=="GlobalMeshing") {
                        auto* pm=ServiceRegistry::instance().getService<ProjectManagerService>();
                        auto* node=pm?pm->findNodeByUuid(uuid):nullptr;
                        if(auto* page=node?dynamic_cast<MeshGlobalMeshingWidget*>(node->getPage()):nullptr)
                            dimension=page->meshingDimension();
                        if(dimension==3) {
                            ContextMenuHub::addAction(menu,this,tr("添加体网格设置"),[this,uuid]{addSmeshSubmesh(3,uuid);});
                            ContextMenuHub::addAction(menu,this,tr("添加扫掠网格"),[this]{addSmeshSweep();});
                            ContextMenuHub::addAction(menu,this,tr("扫掠分块"),[this]{addSmeshPartition();});
                        }
                        if(dimension>=2)
                            ContextMenuHub::addAction(menu,this,tr("添加面网格设置"),[this,uuid]{addSmeshSubmesh(2,uuid);});
                    } else if(dimension==3) {
                        ContextMenuHub::addAction(menu,this,tr("添加面网格设置"),[this,uuid]{addSmeshSubmesh(2,uuid);});
                    }
                    if(dimension>=2)
                        ContextMenuHub::addAction(menu,this,tr("添加边网格设置"),[this,uuid]{addSmeshSubmesh(1,uuid);});
                    if(kind=="SmeshVolumeSubmesh"||kind=="SmeshFaceSubmesh"||
                       kind=="SmeshEdgeSubmesh"||kind=="SmeshSweep"||kind=="SmeshPartition") {
                        if(!menu->isEmpty())ContextMenuHub::addSeparator(menu);
                        ContextMenuHub::addAction(menu,this,tr("删除"),[this,uuid]{removeSmeshNode(uuid);});
                    }
                }
            }
            if (ctx.value(QStringLiteral("module")).toString() != QStringLiteral("Mesh") ||
                ctx.value(QStringLiteral("kind")).toString() != QStringLiteral("BarElementPlacement"))
            {
                return;
            }

            const QString uuid = ctx.value(QStringLiteral("uuid")).toString();
            if (uuid.isEmpty())
                return;

            if (!menu->isEmpty())
                ContextMenuHub::addSeparator(menu);
            ContextMenuHub::addAction(menu, this, QStringLiteral("删除"),
                                      [this, uuid]() { removeBarPlacementNode(uuid); });
        },
        0,
        [](const QVariantMap& ctx) -> bool
        {
            return ctx.value(QStringLiteral("module")).toString() == QStringLiteral("Mesh");
        });
}

void MeshModuleController::removeBarPlacementNode(const QString& uuid)
{
    if (uuid.trimmed().isEmpty() || !m_gridNode)
        return;

    auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>();
    auto* node = pm ? pm->findNodeByUuid(uuid) : nullptr;
    if (!node || node == m_gridNode ||
        node->data(0, ProjectNode::RoleModule).toString() != QStringLiteral("Mesh") ||
        node->data(0, ProjectNode::RoleKind).toString() != QStringLiteral("BarElementPlacement"))
    {
        return;
    }

    QVector<int> recordIds;
    const auto records = barElementRecords();
    for (const BarElementRecordInfo& record : records)
    {
        if (record.placementUuid == uuid)
            recordIds.push_back(record.id);
    }

    const QString question = recordIds.isEmpty()
                                 ? QStringLiteral("确定删除“%1”吗？").arg(node->text(0))
                                 : QStringLiteral("“%1”中已生成锚杆。删除该节点将同时删除这些锚杆，是否继续？")
                                       .arg(node->text(0));
    if (QMessageBox::question(nullptr, QStringLiteral("删除锚杆布置"), question,
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No) != QMessageBox::Yes)
    {
        return;
    }

    // 先删除该布置节点实际生成的杆单元，确认网格修改成功后再删工程树节点。
    for (int recordId : recordIds)
    {
        QString error;
        if (!removeBarElementRecord(recordId, &error))
        {
            QMessageBox::warning(nullptr, QStringLiteral("删除锚杆布置"),
                                 error.isEmpty() ? QStringLiteral("删除已生成锚杆失败，节点未删除。")
                                                 : error);
            return;
        }
    }

    PickManager::instance().clearSelection(QStringLiteral("BarElementPlacement"), uuid, PickMode::Node);
    PickManager::instance().clearSelection(QStringLiteral("BarElementPlacement"), uuid, PickMode::Face);

    auto* parent = dynamic_cast<ProjectNode*>(node->parent());
    if (!parent)
        return;

    // 切回父节点后再销毁页面，避免设置区继续持有已删除页面。
    if (pm)
        pm->setCurrentItem(parent);
    parent->removeChild(node);

    if (BasePage* page = node->getPage())
    {
        page->beforeUnload();
        if (page == m_barElementWidget)
            m_barElementWidget = nullptr;
        node->setPage(nullptr);
        delete page;
    }
    delete node;
}

void MeshModuleController::newProject()
{
    if (m_gridNode || m_hasGrid || m_importedGrid)
        closeProject();
    auto* projectManagerService = ServiceRegistry::instance().getService<ProjectManagerService>();
    m_verticalAxis = 2;
    delete m_importSettingsWidget;
    m_importSettingsWidget = new MeshImportSettingsWidget();
    m_importSettingsWidget->setVerticalAxis(m_verticalAxis);
    m_importSettingsWidget->setSourceFileName(m_sourceFileName);
    connect(m_importSettingsWidget, &MeshImportSettingsWidget::importRequested, this,
            &MeshModuleController::onImportGridFileDialog);
    connect(m_importSettingsWidget, &MeshImportSettingsWidget::verticalAxisChanged, this,
            [this](int axis)
            {
                if (axis < 1 || axis > 3)
                    return;
                m_verticalAxis = axis;
                MeshGridIO::setVerticalAxis(m_importedGrid, m_verticalAxis);
            });
    m_gridNode = new ProjectNode("网格", QUuid::createUuid().toString(), m_importSettingsWidget);
    m_gridNode->setData(0, ProjectNode::RoleModule, "Mesh");
    m_gridNode->setData(0, ProjectNode::RoleKind, "MeshRoot");
    projectManagerService->addTreeItem(m_gridNode);
    m_edgeRules.clear();
    m_faceRules.clear();
    m_volumeRules.clear();
}


bool MeshModuleController::saveProject()
{
    QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();

    if (projectDir.isEmpty())
        return false;

    QJsonObject root;
    root["module"] = "Mesh";
    root["root_title"] = "网格";

    const QString snapshotPath =
        QDir(projectDir).filePath(PublicConstants::kMeshSnapshotFileName);

    const QString meshMapPath =
        QDir(projectDir).filePath(kMeshMapFileName);
    const QString binaryCachePath =
        QDir(projectDir).filePath(QString::fromUtf8(kMeshBinaryCacheFileName));
    const QString surfaceCachePath =
        QDir(projectDir).filePath(QString::fromUtf8(kMeshSurfaceCacheFileName));
    const QString lineCachePath =
        QDir(projectDir).filePath(QString::fromUtf8(kMeshLineCacheFileName));
    const QString legacyOuterSurfaceCachePath =
        QDir(projectDir).filePath(QString::fromUtf8(kMeshLegacyOuterSurfaceCacheFileName));

    // 清理早期性能补丁产生的“仅外表面”缓存，避免它继续留在工程压缩包里造成误解。
    if (QFileInfo::exists(legacyOuterSurfaceCachePath))
        QFile::remove(legacyOuterSurfaceCachePath);

    const bool hasGrid =
        (m_importedGrid && m_importedGrid->GetNumberOfCells() > 0);
    if (hasGrid)
        normalizeGridAttributeTupleCounts(m_importedGrid);

    root["has_grid"] = hasGrid;
    root["snapshot_file"] = PublicConstants::kMeshSnapshotFileName;
    root["mesh_map_file"] = kMeshMapFileName;
    root["binary_cache_file"] = QString::fromUtf8(kMeshBinaryCacheFileName);
    root["surface_cache_file"] = QString::fromUtf8(kMeshSurfaceCacheFileName);
    root["line_cache_file"] = QString::fromUtf8(kMeshLineCacheFileName);
    root["display_cache_version"] = kMeshDisplayCacheVersion;
    // IIIN/DAT 仍是求解与兼容性的权威快照；VTU/VTP 只是可删除、可重建的加载缓存。
    // surface cache 保存的是完整 mapped surface，包含外表面以及 MATYPE/LAYER 分界面。
    // Restore projects from the IIIN/DAT snapshot plus mesh_map.json.
    root["mesh_restore_mode"] = QStringLiteral("dat");
    root["vertical_axis"] = m_verticalAxis;

    if (hasGrid)
    {
        QString err;

        if (shouldRewriteGridSnapshot(snapshotPath, m_importedGrid))
        {
            if (!MeshGridIO::instance().exportIiinFile(snapshotPath, m_importedGrid, &err, m_verticalAxis))
            {
                return false;
            }
            markGridSnapshotSaved(snapshotPath, m_importedGrid);
        }

        if (!saveMeshMapJson(meshMapPath, MeshGridIO::instance().getMeshMapRef(), m_importedGrid, &err))
        {
            return false;
        }

        // 二进制完整网格缓存：下次打开工程优先读取，避免再次解析几十 MB 的文本 IIIN。
        if (!meshCacheIsFresh(binaryCachePath, snapshotPath))
            writeMeshGridCache(binaryCachePath, m_importedGrid);

        // 当前没有自定义筛选时，Mapper 中就是完整 mapped surface + 杆单元网格。
        // 直接把已经构造好的完整显示数据落盘，不在保存时重复遍历全部单元。
        const bool customFilter =
            PublicVariables::instance().get(kMeshDisplayFilterCustomKey).toBool();
        if (!customFilter)
        {
            if (!meshCacheIsFresh(surfaceCachePath, snapshotPath))
            {
                if (auto* mapper = vtkPolyDataMapper::SafeDownCast(m_previewSurfaceMapper))
                {
                    if (auto* surface = mapper->GetInput())
                        writeMeshSurfaceCache(surfaceCachePath, surface);
                }
            }
            if (!meshCacheIsFresh(lineCachePath, snapshotPath))
            {
                if (auto* mapper = vtkDataSetMapper::SafeDownCast(m_previewLineMapper))
                {
                    if (auto* lineGrid = vtkUnstructuredGrid::SafeDownCast(mapper->GetInput()))
                        writeMeshGridCache(lineCachePath, lineGrid);
                }
            }
        }
    }

    auto edgeRulesToJson = [this]()
    {
        QJsonArray arr;

        for (const auto& rule : m_edgeRules)
        {
            QJsonObject obj;
            obj["edge_ids"] = rule.edgeIds;
            obj["max_size"] = rule.maxSize;
            // Keep the legacy values so old builds can still open the project.
            arr.append(obj);
        }

        return arr;
    };

    auto faceRulesToJson = [this]()
    {
        QJsonArray arr;

        for (const auto& rule : m_faceRules)
        {
            QJsonObject obj;
            obj["face_ids"] = rule.faceIds;
            obj["max_size"] = rule.maxSize;
            arr.append(obj);
        }

        return arr;
    };

    auto volumeRulesToJson = [this]()
    {
        QJsonArray arr;

        for (const auto& rule : m_volumeRules)
        {
            QJsonObject obj;
            obj["volume_ids"] = rule.volumeIds;
            obj["max_size"] = rule.maxSize;
            arr.append(obj);
        }

        return arr;
    };

    root["edge_rules"] = edgeRulesToJson();
    root["face_rules"] = faceRulesToJson();
    root["volume_rules"] = volumeRulesToJson();

    QJsonArray savedBarGroups;
    if (hasGrid)
    {
        vtkDataArray* groupIds = m_importedGrid->GetCellData()->GetArray("BAR_GROUP");
        vtkDataArray* elementIds = m_importedGrid->GetCellData()->GetArray("elem_id");
        auto* groupNames = vtkStringArray::SafeDownCast(
            m_importedGrid->GetCellData()->GetAbstractArray("BAR_GROUP_NAME"));
        QMap<int, QJsonObject> groups;
        QMap<int, QJsonArray> groupElements;
        for (vtkIdType cid = 0; groupIds && cid < m_importedGrid->GetNumberOfCells(); ++cid)
        {
            const int groupId = static_cast<int>(groupIds->GetTuple1(cid));
            vtkCell* cell = m_importedGrid->GetCell(cid);
            if (groupId <= 0 || !cell || cell->GetCellDimension() != 1)
                continue;
            QJsonObject group = groups.value(groupId);
            group[QStringLiteral("id")] = groupId;
            if (groupNames && cid < groupNames->GetNumberOfValues())
                group[QStringLiteral("name")] =
                    QString::fromStdString(groupNames->GetValue(cid));
            groups.insert(groupId, group);
            groupElements[groupId].append(
                elementIds ? static_cast<int>(elementIds->GetTuple1(cid))
                           : static_cast<int>(cid + 1));
        }
        for (auto it = groups.constBegin(); it != groups.constEnd(); ++it)
        {
            QJsonObject group = it.value();
            group[QStringLiteral("element_ids")] = groupElements.value(it.key());
            savedBarGroups.append(group);
        }
    }
    root[QStringLiteral("bar_element_groups")] = savedBarGroups;

    QJsonArray nodeItems;

    std::function<QJsonObject(ProjectNode*)> saveNode = [&](ProjectNode* node) -> QJsonObject
    {
        QJsonObject obj;

        if (!node)
            return obj;

        obj["uuid"] = node->data(0, ProjectNode::RoleUuid).toString();
        obj["title"] = node->text(0);
        obj["module"] = node->data(0, ProjectNode::RoleModule).toString();
        obj["kind"] = node->data(0, ProjectNode::RoleKind).toString();
        if (BasePage* page = node->getPage())
        {
            const QJsonObject pageData = page->saveJson();
            if (!pageData.isEmpty())
                obj["page_data"] = pageData;
        }

        QJsonArray children;

        for (int i = 0; i < node->childCount(); ++i)
        {
            auto* child = dynamic_cast<ProjectNode*>(node->child(i));

            if (!child)
                continue;

            children.append(saveNode(child));
        }

        obj["children"] = children;
        return obj;
    };

    if (m_gridNode)
    {
        for (int i = 0; i < m_gridNode->childCount(); ++i)
        {
            auto* child = dynamic_cast<ProjectNode*>(m_gridNode->child(i));

            if (!child)
                continue;

            nodeItems.append(saveNode(child));
        }
    }

    root["nodes"] = nodeItems;

    QFile file(QDir(projectDir).filePath(PublicConstants::kMeshProjectFileName));

    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;

    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    file.close();

    return true;
}

bool MeshModuleController::loadProject()
{
    if (m_gridNode || m_hasGrid || m_importedGrid)
        closeProject();
    auto* projectManagerService = ServiceRegistry::instance().getService<ProjectManagerService>();

    delete m_importSettingsWidget;
    m_importSettingsWidget = new MeshImportSettingsWidget();
    connect(m_importSettingsWidget, &MeshImportSettingsWidget::importRequested, this,
            &MeshModuleController::onImportGridFileDialog);
    connect(m_importSettingsWidget, &MeshImportSettingsWidget::verticalAxisChanged, this,
            [this](int axis)
            {
                if (axis < 1 || axis > 3)
                    return;
                m_verticalAxis = axis;
                MeshGridIO::setVerticalAxis(m_importedGrid, m_verticalAxis);
            });
    m_gridNode = new ProjectNode("网格", QUuid::createUuid().toString(), m_importSettingsWidget);
    m_gridNode->setData(0, ProjectNode::RoleModule, "Mesh");
    m_gridNode->setData(0, ProjectNode::RoleKind, "MeshRoot");
    projectManagerService->addTreeItem(m_gridNode);

    QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();

    if (projectDir.isEmpty())
        return false;

    QFile file(QDir(projectDir).filePath(PublicConstants::kMeshProjectFileName));

    if (!file.open(QIODevice::ReadOnly))
        return true;

    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    file.close();

    const bool hasSavedVerticalAxis = root.contains("vertical_axis");
    m_verticalAxis = root.value("vertical_axis").toInt(2);
    if (m_verticalAxis < 1 || m_verticalAxis > 3)
        m_verticalAxis = 2;
    if (m_importSettingsWidget)
        m_importSettingsWidget->setVerticalAxis(m_verticalAxis);

    const bool hasGrid = root.value("has_grid").toBool(false);

    if (hasGrid)
    {
        const QString snapshotName =
            root.value("snapshot_file").toString(PublicConstants::kMeshSnapshotFileName);

        const QString meshMapName =
            root.value("mesh_map_file").toString(kMeshMapFileName);
        const QString binaryCacheName =
            root.value("binary_cache_file").toString(QString::fromUtf8(kMeshBinaryCacheFileName));
        const QString surfaceCacheName =
            root.value("surface_cache_file").toString(QString::fromUtf8(kMeshSurfaceCacheFileName));
        const QString lineCacheName =
            root.value("line_cache_file").toString(QString::fromUtf8(kMeshLineCacheFileName));
        const int displayCacheVersion = root.value("display_cache_version").toInt(0);

        QString snapshotPath = QDir(projectDir).filePath(snapshotName);
        QString meshMapPath = QDir(projectDir).filePath(meshMapName);
        const QString binaryCachePath = QDir(projectDir).filePath(binaryCacheName);
        const QString surfaceCachePath = QDir(projectDir).filePath(surfaceCacheName);
        const QString lineCachePath = QDir(projectDir).filePath(lineCacheName);
        if (!QFileInfo::exists(snapshotPath))
        {
            snapshotPath = QDir(projectDir).filePath(
                QString::fromUtf8(PublicConstants::kMeshSnapshotFileName));
        }
        if (!QFileInfo::exists(meshMapPath))
        {
            meshMapPath = QDir(projectDir).filePath(
                QString::fromUtf8(PublicConstants::kMeshMapFileName));
        }

        QString err;
        // 优先读取二进制 VTU，但它只是缓存，不能覆盖 IIIN 的权威拓扑。
        // 旧版本曾把部分可正常恢复的退化单元缓存为“异常/Polyhedron”；
        // 即使 MeshGridIO 的识别规则已经修好，只要继续命中旧 VTU，1099 号这类
        // A B C C D E F F 三棱柱仍会沿用错误分类。因此拓扑版本不匹配时
        // 强制从 IIIN 重建一次，并立即刷新二进制缓存。
        vtkSmartPointer<vtkUnstructuredGrid> grid;
        bool rebuiltTopologyCache = false;
        if (meshCacheIsFresh(binaryCachePath, snapshotPath))
        {
            auto cachedGrid = readMeshGridCache(binaryCachePath);
            if (meshGridHasCurrentIiinTopologySchema(cachedGrid))
            {
                grid = cachedGrid;
            }
            else if (cachedGrid && cachedGrid->GetNumberOfCells() > 0)
            {
                qInfo() << "[Mesh] topology cache schema is stale; rebuild from IIIN:"
                        << binaryCachePath;
                rebuiltTopologyCache = true;
            }
        }
        if (!grid || grid->GetNumberOfCells() <= 0)
        {
            grid = MeshGridIO::instance().loadIiinFile(snapshotPath, &err);
            if (grid && grid->GetNumberOfCells() > 0)
            {
                rebuiltTopologyCache = true;
                // 迁移只做一次：本次打开工程就覆盖旧 VTU，下一次恢复重新走快速缓存。
                writeMeshGridCache(binaryCachePath, grid);
            }
        }
        if (!grid || grid->GetNumberOfCells() <= 0)
        {
            QMessageBox::warning(nullptr,
                                 QStringLiteral("工程网格读取失败"),
                                 err.isEmpty()
                                     ? QStringLiteral("工程网格快照读取失败。")
                                     : err);
            return false;
        }

        m_importedGrid = grid;
        if (normalizeRodElementLayers(m_importedGrid))
            writeMeshGridCache(binaryCachePath, m_importedGrid);
        if (!hasSavedVerticalAxis)
            m_verticalAxis = MeshGridIO::verticalAxisOfGrid(m_importedGrid, m_verticalAxis);
        MeshGridIO::setVerticalAxis(m_importedGrid, m_verticalAxis);
        m_hasGrid = true;
        m_sourceFileName.clear();
        if (m_importSettingsWidget)
        {
            m_importSettingsWidget->setVerticalAxis(m_verticalAxis);
            m_importSettingsWidget->setSourceFileName(m_sourceFileName);
        }

        const QJsonArray savedBarGroups = root.value(QStringLiteral("bar_element_groups")).toArray();
        if (!savedBarGroups.isEmpty())
        {
            const vtkIdType cellCount = m_importedGrid->GetNumberOfCells();
            auto groupIds = vtkSmartPointer<vtkIntArray>::New();
            groupIds->SetName("BAR_GROUP");
            groupIds->SetNumberOfComponents(1);
            groupIds->SetNumberOfTuples(cellCount);
            groupIds->FillComponent(0, 0);
            auto groupNames = vtkSmartPointer<vtkStringArray>::New();
            groupNames->SetName("BAR_GROUP_NAME");
            groupNames->SetNumberOfValues(cellCount);
            for (vtkIdType cid = 0; cid < cellCount; ++cid)
                groupNames->SetValue(cid, std::string());

            QHash<int, QPair<int, QString>> groupByElementId;
            for (const QJsonValue& value : savedBarGroups)
            {
                const QJsonObject group = value.toObject();
                const int groupId = group.value(QStringLiteral("id")).toInt();
                const QString name = group.value(QStringLiteral("name")).toString();
                for (const QJsonValue& element : group.value(QStringLiteral("element_ids")).toArray())
                    groupByElementId.insert(element.toInt(), qMakePair(groupId, name));
            }
            vtkDataArray* elementIds = m_importedGrid->GetCellData()->GetArray("elem_id");
            for (vtkIdType cid = 0; cid < cellCount; ++cid)
            {
                const int elementId = elementIds
                                          ? static_cast<int>(elementIds->GetTuple1(cid))
                                          : static_cast<int>(cid + 1);
                const auto it = groupByElementId.constFind(elementId);
                if (it == groupByElementId.constEnd())
                    continue;
                groupIds->SetValue(cid, it.value().first);
                groupNames->SetValue(cid, it.value().second.toStdString());
            }
            m_importedGrid->GetCellData()->AddArray(groupIds);
            m_importedGrid->GetCellData()->AddArray(groupNames);
        }

        normalizeGridAttributeTupleCounts(m_importedGrid);

        m_splitCacheSource = nullptr;
        m_splitCacheLine = nullptr;
        m_splitCacheNonLine = nullptr;

        /*
         * 新工程：
         * 有 mesh_map.json，则恢复 Physical 边界、node_id 边界映射等。
         *          * 旧工程：
         * 没有 mesh_map.json，不能失败。
         * 直接从 DAT 网格里的 MATYPE/LAYER 重建域分组即可。
         */
        if (QFileInfo::exists(meshMapPath))
        {
            if (!loadMeshMapJson(meshMapPath, m_importedGrid, &err))
            {
                syncPickMapGroupingFromGrid(m_importedGrid);
            }
            else
            {
                // 新版 mesh_map.json 不再保存 MATYPE/LAYER 的大 cellIds，
                // 这里统一从网格 CellData 重建材料/层级分组和 cellId 反查缓存。
                syncPickMapGroupingFromGrid(m_importedGrid);
            }
        }
        else
        {
            syncPickMapGroupingFromGrid(m_importedGrid);
        }

        markGridSnapshotSaved(snapshotPath, m_importedGrid);

        // 显示缓存只在与当前 DAT 快照一致时使用。完整 surface 缓存命中后，
        // 首次显示可直接跳过 meshDisplayBuildMappedSurface() 的百万级共享面哈希构造，
        // 但显示内容与原逻辑保持一致（外表面 + MATYPE/LAYER 分界面）。
        m_loadedSurfaceDisplayCache = nullptr;
        m_loadedLineDisplayCache = nullptr;

        // 完整网格拓扑发生迁移时，旧 surface/line 显示缓存也是由旧拓扑生成的，
        // 不能继续命中，否则完整网格已经把 1099 识别成三棱柱，画面却仍可能来自
        // 上一版的异常 Polyhedron。直接删掉，让本次显示按新拓扑重建。
        if (rebuiltTopologyCache)
        {
            QFile::remove(surfaceCachePath);
            QFile::remove(lineCachePath);
        }

        // v1 性能补丁曾生成过“仅外表面”的 surface cache，不能继续复用。
        // 只有明确标记为当前完整 mapped-surface 版本的缓存才允许命中。
        if (!rebuiltTopologyCache && displayCacheVersion == kMeshDisplayCacheVersion &&
            meshCacheIsFresh(surfaceCachePath, snapshotPath))
        {
            m_loadedSurfaceDisplayCache = readMeshSurfaceCache(surfaceCachePath);
        }
        if (!rebuiltTopologyCache && displayCacheVersion == kMeshDisplayCacheVersion &&
            meshCacheIsFresh(lineCachePath, snapshotPath))
            m_loadedLineDisplayCache = readMeshGridCache(lineCachePath);
        // 工程网格恢复完成后自动适应一次视图。
        displayCurrentGrid(true);

        PickManager::instance().clearDomainSelection(QStringLiteral("MaterialManager"));
    }

    m_edgeRules.clear();

    for (const auto& v : root.value("edge_rules").toArray())
    {
        const QJsonObject obj = v.toObject();

        EdgeControlRule rule;
        rule.edgeIds = obj.value("edge_ids").toString();
        rule.maxSize = obj.value("max_size").toDouble(0.0);

        m_edgeRules.push_back(rule);
    }

    m_faceRules.clear();

    for (const auto& v : root.value("face_rules").toArray())
    {
        const QJsonObject obj = v.toObject();

        FaceControlRule rule;
        rule.faceIds = obj.value("face_ids").toString();
        rule.maxSize = obj.value("max_size").toDouble(0.0);

        m_faceRules.push_back(rule);
    }

    m_volumeRules.clear();

    for (const auto& v : root.value("volume_rules").toArray())
    {
        const QJsonObject obj = v.toObject();

        VolumeControlRule rule;
        rule.volumeIds = obj.value("volume_ids").toString();
        rule.maxSize = obj.value("max_size").toDouble(0.0);

        m_volumeRules.push_back(rule);
    }

    auto createPageByKind = [this](const QString& kind) -> BasePage*
    {
        if (kind == "GlobalMeshing")
            return new MeshGlobalMeshingWidget();

        if (kind == "SingleMeshing")
            return new MeshSingleMeshingWidget();
        if (kind == "SmeshVolumeSubmesh" || kind == "SmeshFaceSubmesh" || kind == "SmeshEdgeSubmesh") {
            auto* page=new MeshSmeshSubmeshWidget(kind=="SmeshVolumeSubmesh"?3:kind=="SmeshFaceSubmesh"?2:1);
            bindSmeshPage(page); return page;
        }
        if (kind == "SmeshSweep") {
            auto* page=new MeshSmeshSweepWidget();bindSmeshPage(page);return page;
        }
        if (kind == "SmeshPartition") {
            auto* page=new MeshSmeshPartitionWidget();bindSmeshPage(page);return page;
        }

        if (kind == "MeshGrouping")
            return new MeshGroupingWidget();

        if (kind == "BarElementPlacement")
        {
            auto* page = new BarElementPlacementWidget();
            m_barElementWidget = page;
            return page;
        }

        if (kind == "MeshQualityCheck")
        {
            auto* page = new MeshQualityCheckWidget();
            m_qualityCheckWidget = page;
            connect(page, &MeshQualityCheckWidget::checkRequested,
                    this, &MeshModuleController::runQualityCheck);
            connect(page, &MeshQualityCheckWidget::stopRequested,
                    this, &MeshModuleController::stopQualityCheck);
            return page;
        }

        if (kind == "MeshImportSettings")
            return nullptr;


        if (kind == "MeshEdit")
        {
            ensureGridDisplayControlPanel();
            auto* w = m_gridEditingWidget;

            if (w && m_importedGrid && m_importedGrid->GetNumberOfCells() > 0)
            {
                w->setEditableGrid(m_importedGrid, m_sourceFileName);
            }

            return w;
        }

        return nullptr;
    };

    std::function<void(ProjectNode*, const QJsonObject&)> loadNode =
        [&](ProjectNode* parent, const QJsonObject& obj)
    {
        if (!parent)
            return;

        const QString kind = obj.value("kind").toString();

        BasePage* page = createPageByKind(kind);

        if (!page)
            return;

        const QString title = kind == "MeshEdit" ? QStringLiteral("显示控制")
                                                   : obj.value("title").toString(kind);
        const QString uuid = obj.value("uuid").toString(QUuid::createUuid().toString());

        auto* node = new ProjectNode(title, uuid, page);
        node->setData(0, ProjectNode::RoleModule, "Mesh");
        node->setData(0, ProjectNode::RoleKind, kind);
        node->setData(0, ProjectNode::RoleUuid, uuid);

        page->loadJson(obj.value("page_data").toObject());
        if(auto* sweep=dynamic_cast<MeshSmeshSweepWidget*>(page)) {
            sweep->setMeshPlanIdentity(uuid,title);
            sweep->setPartitionPlanIdentity(parent->getUuid());
        }
        if(auto* partition=dynamic_cast<MeshSmeshPartitionWidget*>(page))
            partition->setMeshPlanIdentity(parent->getUuid(),parent->text(0));
        if(auto* local=dynamic_cast<MeshSmeshSubmeshWidget*>(page)) {
            const QString oldDefault=local->settings().dimension==3?tr("体 SubMesh"):
                local->settings().dimension==2?tr("面 SubMesh"):tr("边 SubMesh");
            const QString oldTitle=obj.value("title").toString();
            if(oldTitle==oldDefault || oldTitle==kind || oldTitle.isEmpty()) {
                const QString prefix=local->settings().name;
                QSet<QString> names;
                for(auto* child:parent->getChildren())names.insert(child->text(0));
                int number=1;
                while(names.contains(prefix+QStringLiteral("-%1").arg(number)))++number;
                local->setDisplayName(prefix+QStringLiteral("-%1").arg(number));
                node->setText(0,local->settings().name);
            }
        } else if(auto* sweep=dynamic_cast<MeshSmeshSweepWidget*>(page)) {
            if(title==tr("扫掠剖分") || title==kind) {
                const QString prefix=sweep->settings().name;
                QSet<QString> names;
                for(auto* child:parent->getChildren())names.insert(child->text(0));
                int number=1;
                while(names.contains(prefix+QStringLiteral("-%1").arg(number)))++number;
                sweep->setDisplayName(prefix+QStringLiteral("-%1").arg(number));
                node->setText(0,sweep->settings().name);
            }
        } else if(kind=="GlobalMeshing" && title==tr("网格划分")) {
            node->setText(0,tr("整体剖分"));
        }

        parent->addChild(node);

        const QJsonArray children = obj.value("children").toArray();

        for (const auto& childVal : children)
        {
            loadNode(node, childVal.toObject());
        }
    };

    const QJsonArray nodes = root.value("nodes").toArray();

    for (const auto& n : nodes)
    {
        loadNode(m_gridNode, n.toObject());
    }

    // Retire the old size-control pages without losing their saved CAD sizes.
    // Valid legacy rules become editable SMESH settings and are removed from
    // the legacy lists, so the mesher never applies both representations.
    auto migrateSizeRule = [this](int dimension, const QString& text, double size)
    {
        if (!std::isfinite(size) || size <= 0.0)
            return false;
        QSet<int> selected;
        for (const QString& part : text.split(',', Qt::SkipEmptyParts))
        {
            const auto bounds = part.trimmed().split('-');
            bool firstOk = false, lastOk = true;
            const int first = bounds.value(0).trimmed().toInt(&firstOk);
            const int last = bounds.size() == 1 ? first : bounds.value(1).trimmed().toInt(&lastOk);
            if (bounds.size() > 2 || !firstOk || !lastOk || first <= 0 || last < first || last > 1000000)
                return false;
            for (int id = first; id <= last; ++id)
                selected.insert(id);
        }
        if (selected.isEmpty())
            return false;

        auto* parent = findDirectChildByKind(m_gridNode, "GlobalMeshing");
        if (!parent)
        {
            parent = new ProjectNode(tr("整体剖分"), QUuid::createUuid().toString(), new MeshGlobalMeshingWidget());
            parent->setData(0, ProjectNode::RoleModule, "Mesh");
            parent->setData(0, ProjectNode::RoleKind, "GlobalMeshing");
            parent->setData(0, ProjectNode::RoleUuid, parent->getUuid());
            m_gridNode->addChild(parent);
        }
        QSet<QString> names;
        for (auto* child : parent->getChildren())
        {
            names.insert(child->text(0));
            if (auto* existing = dynamic_cast<MeshSmeshSubmeshWidget*>(child->getPage()))
            {
                const auto settings = existing->settings();
                const QSet<int> ids(settings.entityIds.cbegin(), settings.entityIds.cend());
                if (settings.dimension == dimension && ids == selected && settings.maxSize == size)
                    return true;
                // A second sibling with overlapping CAD ids is invalid in SMESH.
                // Keep that legacy restriction in its supported data format
                // rather than changing an existing task's parameters on load.
                if (settings.dimension == dimension && ids.intersects(selected))
                    return false;
            }
        }
        const QString prefix = dimension == 3 ? tr("体网格设置") : dimension == 2 ? tr("面网格设置") : tr("边网格设置");
        int number = 1;
        while (names.contains(prefix + QStringLiteral("-%1").arg(number))) ++number;
        QList<int> ordered = selected.values();
        std::sort(ordered.begin(), ordered.end());
        QJsonArray ids;
        for (int id : ordered) ids.append(id);
        auto* page = new MeshSmeshSubmeshWidget(dimension);
        const QString name = prefix + QStringLiteral("-%1").arg(number);
        page->loadJson({{"name", name}, {"entity_ids", ids}, {"max_size", size}});
        bindSmeshPage(page);
        auto* node = new ProjectNode(name, QUuid::createUuid().toString(), page);
        node->setData(0, ProjectNode::RoleModule, "Mesh");
        node->setData(0, ProjectNode::RoleKind,
                      dimension == 3 ? "SmeshVolumeSubmesh" : dimension == 2 ? "SmeshFaceSubmesh" : "SmeshEdgeSubmesh");
        node->setData(0, ProjectNode::RoleUuid, node->getUuid());
        parent->addChild(node);
        return true;
    };
    m_edgeRules.removeIf([&](const EdgeControlRule& rule) { return migrateSizeRule(1, rule.edgeIds, rule.maxSize); });
    m_faceRules.removeIf([&](const FaceControlRule& rule) { return migrateSizeRule(2, rule.faceIds, rule.maxSize); });
    m_volumeRules.removeIf([&](const VolumeControlRule& rule) { return migrateSizeRule(3, rule.volumeIds, rule.maxSize); });

    updateQualityCheckContext(false);

    // The dock survives project changes even when this project has no saved
    // MeshEdit tree node. closeProject() cleared it, so refresh its source here.
    if (m_gridEditingWidget)
    {
        if (m_hasGrid && m_importedGrid)
            m_gridEditingWidget->setEditableGrid(m_importedGrid, m_sourceFileName);
        else
            m_gridEditingWidget->clearGrid();
    }

    return true;
}

void MeshModuleController::closeProject()
{
    MeshSmeshWorkflowState::reset();
    if (auto* qualityProcess = findChild<QProcess*>(QStringLiteral("gridQualityProcess"));
        qualityProcess && qualityProcess->state() != QProcess::NotRunning)
    {
        qualityProcess->setProperty("qualityStopRequested", true);
        qualityProcess->kill();
        qualityProcess->waitForFinished(3000);
    }

    ProjectNode* root = m_gridNode;
    m_gridNode = nullptr;
    if (root)
    {
        if (auto* parent = dynamic_cast<ProjectNode*>(root->parent()))
            parent->removeChild(root);
        else if (QTreeWidget* tree = root->treeWidget())
        {
            const int index = tree->indexOfTopLevelItem(root);
            if (index >= 0)
                tree->takeTopLevelItem(index);
        }

        std::function<void(ProjectNode*)> destroyNode = [&](ProjectNode* node)
        {
            if (!node)
                return;
            const auto children = node->getChildren();
            for (ProjectNode* child : children)
            {
                node->removeChild(child);
                destroyNode(child);
            }
            if (BasePage* page = node->getPage())
            {
                if (page == m_gridEditingWidget)
                {
                    node->setPage(nullptr);
                }
                else
                {
                    page->beforeUnload();
                    delete page;
                }
                node->setPage(nullptr);
            }
            delete node;
        };
        destroyNode(root);
    }

    m_importSettingsWidget = nullptr;
    if (m_gridEditingWidget)
    {
        m_gridEditingWidget->clearGrid();
    }
    m_barElementWidget = nullptr;
    m_qualityCheckWidget = nullptr;

    if (m_visualWidgetService)
    {
        const QStringList propIds = {
            QStringLiteral("mesh.imported.surface"),
            QStringLiteral("mesh.imported.line"),
            QStringLiteral("mesh.global.surf"),
            QStringLiteral("mesh.global.wire"),
            QStringLiteral("mesh.face.control.highlight"),
            QStringLiteral("mesh.edge.control.highlight"),
            QStringLiteral("mesh.volume.control.highlight"),
            QStringLiteral("mesh.grouping.range.preview")};
        for (const QString& propId : propIds)
            m_visualWidgetService->removeProp(propId, false);
    }

    m_importedGrid = nullptr;
    m_hasGrid = false;
    m_sourceFileName.clear();
    m_verticalAxis = 2;
    m_previewSurfaceMapper = nullptr;
    m_previewSurfaceActor = nullptr;
    m_previewSurfaceGrid = nullptr;
    m_previewLineMapper = nullptr;
    m_previewLineActor = nullptr;
    m_previewLineGrid = nullptr;
    m_previewMapper = nullptr;
    m_previewActor = nullptr;
    m_previewGrid = nullptr;
    m_splitCacheSource = nullptr;
    m_splitCacheLine = nullptr;
    m_splitCacheNonLine = nullptr;
    m_loadedSurfaceDisplayCache = nullptr;
    m_loadedLineDisplayCache = nullptr;
    m_edgeRules.clear();
    m_faceRules.clear();
    m_volumeRules.clear();

    MeshGridIO::instance().setMeshMap(MshPickMap{});
    PickManager::instance().resetProjectState();

    auto& variables = PublicVariables::instance();
    const QStringList meshKeys = variables.keysForOwner(QStringLiteral("mesh"));
    for (const QString& key : meshKeys)
        variables.remove(key, QStringLiteral("mesh"));

    if (m_visualWidgetService)
        m_visualWidgetService->render();
}

QVector<MeshTagSet> MeshModuleController::domainTagSets() const
{
    QVector<MeshTagSet> out;
    const MshPickMap& map = MeshGridIO::instance().getMeshMapRef();
    for (auto it = map.tagSets.constBegin(); it != map.tagSets.constEnd(); ++it)
    {
        const MeshTagSet& set = it.value();
        if (set.key.dim == map.meshDim && !set.cellIds.isEmpty())
            out.push_back(set);
    }

    std::sort(out.begin(), out.end(), [](const MeshTagSet& a, const MeshTagSet& b)
              {
                  if (a.key.category != b.key.category)
                      return a.key.category < b.key.category;
                  return a.key.id < b.key.id;
              });
    return out;
}

QString MeshModuleController::materialGroupCategory() const
{
    return MeshGridIO::instance().getMeshMapRef().materialGroupCategory.trimmed();
}

QString MeshModuleController::excavationGroupCategory() const
{
    return MeshGridIO::instance().getMeshMapRef().excavationGroupCategory.trimmed();
}

QVector<MeshTagSet> MeshModuleController::boundaryTagSets(int dimFilter) const
{
    const MshPickMap& map = MeshGridIO::instance().getMeshMapRef();

    QHash<MeshTagKey, MeshTagSet> sets;

           // 主入口：tagSets。这里是 UI 和业务层唯一应该依赖的边界模型。
    for (auto it = map.tagSets.constBegin(); it != map.tagSets.constEnd(); ++it)
    {
        MeshTagSet set = it.value();
        const bool isPhysical =
            set.key.category.compare(QStringLiteral("Physical"), Qt::CaseInsensitive) == 0;
        const bool isSmeshBoundary =
            set.key.category.compare(QStringLiteral("SmeshBoundary"),
                                     Qt::CaseInsensitive) == 0;
        if (!isPhysical && !isSmeshBoundary)
            continue;
        if (set.key.dim < 0 || set.key.dim >= map.meshDim)
            continue;
        if (dimFilter >= 0 && set.key.dim != dimFilter)
            continue;

        if (set.pointIds.isEmpty() && set.edgeKeys.isEmpty() && set.faceKeys.isEmpty())
            continue;

        normalizeMeshTagSet(set);
        sets.insert(set.key, set);
    }

           // 不再从任何旧映射兜底；Controller 对外只返回 tagSets。
    return sortedMeshTagSets(sets);
}

QVector<MeshIdType> MeshModuleController::cellIdsOfTag(const MeshTagKey& key) const
{
    if (!key.isValid())
        return {};

    const MshPickMap& map = MeshGridIO::instance().getMeshMapRef();
    if (map.tagSets.contains(key))
    {
        QVector<MeshIdType> ids = map.tagSets.value(key).cellIds;
        std::sort(ids.begin(), ids.end());
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
        return ids;
    }
    return {};
}

QVector<MeshIdType> MeshModuleController::pointIdsOfTag(const MeshTagKey& key) const
{
    if (!key.isValid())
        return {};

    const MshPickMap& map = MeshGridIO::instance().getMeshMapRef();
    if (map.tagSets.contains(key))
    {
        QVector<MeshIdType> ids = map.tagSets.value(key).pointIds;
        std::sort(ids.begin(), ids.end());
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
        return ids;
    }

    return {};
}

QString MeshModuleController::displayTextOfTag(const MeshTagKey& key) const
{
    const MshPickMap& map = MeshGridIO::instance().getMeshMapRef();

    if (map.tagSets.contains(key))
    {
        const QString name = normalizePhysicalUiName(key.dim, key.id, map.tagSets.value(key).name);
        if (key.category == QStringLiteral("Physical") ||
            key.category.compare(QStringLiteral("REGION"), Qt::CaseInsensitive) == 0 ||
            key.category.compare(QStringLiteral("SmeshBoundary"),
                                 Qt::CaseInsensitive) == 0)
            return name;
        return QStringLiteral("%1：%2").arg(key.category, name);
    }

    if (key.category == QStringLiteral("Physical"))
        return normalizePhysicalUiName(key.dim, key.id, map.physIdToName.value(key.id));

    return QStringLiteral("%1=%2").arg(key.category).arg(key.id);
}


QSet<int> MeshModuleController::getAvailableMatypes() const
{
    QSet<int> ret;
    const MshPickMap& map = MeshGridIO::instance().getMeshMapRef();

    for (auto it = map.tagSets.constBegin(); it != map.tagSets.constEnd(); ++it)
    {
        const MeshTagSet& set = it.value();

               // 材料编号来自 MATYPE，既可能在实体/面单元，也可能在杆单元。
               // 这里不能只返回 map.meshDim 的最高维域，否则 lineGrid 独有的 MATYPE 无法出现在材料下拉框中。
        if (set.cellIds.isEmpty())
            continue;

        if (set.key.category.compare("MATYPE", Qt::CaseInsensitive) != 0)
            continue;

        if (set.key.id < FIRST_COMPUTABLE_MATYPE)
            continue;

        ret.insert(set.key.id);
    }

           // 兼容拆分后的 lineGrid：如果 tagSets 旧数据还没同步，也从 lineGrid 的 CellData/MATYPE 里补充。
    auto appendMatypesFromGrid = [&ret](vtkUnstructuredGrid* grid)
    {
        auto* arr = grid && grid->GetCellData()
        ? vtkIntArray::SafeDownCast(grid->GetCellData()->GetArray("MATYPE"))
        : nullptr;
        if (!grid || !arr)
            return;

        const vtkIdType n = std::min(grid->GetNumberOfCells(), arr->GetNumberOfTuples());
        for (vtkIdType cid = 0; cid < n; ++cid)
        {
            const int id = arr->GetValue(cid);
            if (id >= FIRST_COMPUTABLE_MATYPE)
                ret.insert(id);
        }
    };

    appendMatypesFromGrid(m_previewSurfaceGrid);
    appendMatypesFromGrid(m_previewLineGrid);

    return ret;
}

QVector<DomainKey> MeshModuleController::availableDomains(const QString& category) const
{
    QVector<DomainKey> out;
    const auto sets = domainTagSets();
    for (const MeshTagSet& set : sets)
    {
        if (!category.trimmed().isEmpty() &&
            set.key.category.compare(category, Qt::CaseInsensitive) != 0)
            continue;
        out.push_back(DomainKey{set.key.category, set.key.dim, set.key.id});
    }
    return out;
}

QSet<MeshIdType> MeshModuleController::domainCells(const DomainKey& key) const
{
    QSet<MeshIdType> out;
    if (!meshDomainKeyIsValid(key))
        return out;

    const MeshTagKey tagKey{key.category, key.dim, key.id};
    for (MeshIdType cid : cellIdsOfTag(tagKey))
        out.insert(cid);
    return out;
}

QString MeshModuleController::domainDisplayText(const DomainKey& key) const
{
    if (!meshDomainKeyIsValid(key))
        return QString();
    return displayTextOfTag(MeshTagKey{key.category, key.dim, key.id});
}

void MeshModuleController::highlightDomainMatypes(const QSet<int>& matypes, int selectedMatype,
                                                  int previewMatype, int preDeleteMatype)
{
    Q_UNUSED(selectedMatype);

    if (!m_visualWidgetService)
        return;

    QList<vtkMapper*> targetMappers;
    if (m_previewSurfaceMapper)
        targetMappers.push_back(m_previewSurfaceMapper);
    if (m_previewLineMapper)
        targetMappers.push_back(m_previewLineMapper);
    if (targetMappers.isEmpty() && m_previewMapper)
        targetMappers.push_back(m_previewMapper);
    if (targetMappers.isEmpty())
        return;

    const bool hasSelection =
        !matypes.isEmpty() || previewMatype >= 0 || preDeleteMatype >= 0;

    if (!hasSelection)
    {
        // 清除材料临时高亮后，恢复全局显示控制的颜色状态。
        meshDisplayApplyGlobalState(m_importedGrid,
                                    m_previewSurfaceActor,
                                    m_previewSurfaceMapper,
                                    m_previewLineActor,
                                    m_previewLineMapper);
        PickManager::instance().updateStructure();
        m_visualWidgetService->render();
        return;
    }

    QSet<int> allMatypes = getAvailableMatypes();
    if (previewMatype >= 0)
        allMatypes.insert(previewMatype);
    if (preDeleteMatype >= 0)
        allMatypes.insert(preDeleteMatype);
    allMatypes.unite(matypes);

    int maxMatype = -1;
    for (int v : allMatypes)
        maxMatype = qMax(maxMatype, v);
    if (maxMatype < 0)
        return;

    auto lut = vtkSmartPointer<vtkLookupTable>::New();
    lut->SetNumberOfTableValues(maxMatype + 1);
    lut->SetTableRange(0.0, maxMatype);
    lut->Build();

    for (int i = 0; i <= maxMatype; ++i)
        lut->SetTableValue(i, 0.75, 0.75, 0.75, 1.0);

    for (int matype : matypes)
    {
        if (matype >= 0 && matype <= maxMatype)
            lut->SetTableValue(matype, 0.25, 0.75, 0.25, 1.0);
    }
    if (previewMatype >= 0 && previewMatype <= maxMatype)
        lut->SetTableValue(previewMatype, 0.2, 0.4, 0.9, 1.0);
    if (preDeleteMatype >= 0 && preDeleteMatype <= maxMatype)
        lut->SetTableValue(preDeleteMatype, 0.9, 0.55, 0.2, 1.0);

    for (auto* mapper : targetMappers)
    {
        if (!mapper)
            continue;
        mapper->SetScalarModeToUseCellFieldData();
        mapper->SelectColorArray("MATYPE");
        mapper->SetLookupTable(lut);
        mapper->UseLookupTableScalarRangeOn();
        mapper->ScalarVisibilityOn();
    }

    m_visualWidgetService->render();
}

void MeshModuleController::initAction()
{
    auto& em = EventManager::instance();

    auto reg = [&](const char* actionId, std::function<void()> fn)
    {
        em.registerHandler(actionId,
                           [this, fn](const QVariantMap&)
                           {
                               fn();
                           });
    };

           // --- IO ---
    reg(PublicConstants::kActionPreprocessImportGrid,
        [this]
        {
            onImportGrid();
        });
    reg(PublicConstants::kActionPreprocessExportGrid,
        [this]
        {
            onExportGrid();
        });

           // --- Meshing ---
    reg(PublicConstants::kActionPreprocessMeshGlobal,
        [this]
        {
            onMeshGlobal();
        });
    reg(PublicConstants::kActionPreprocessMeshSingle,
        [this]
        {
            onMeshSingle();
        });
    reg(PublicConstants::kActionPreprocessMeshLocalFace,
        [this] { onMeshLocalFace(); });
    reg(PublicConstants::kActionPreprocessMeshLocalEdge,
        [this] { onMeshLocalEdge(); });
    reg(PublicConstants::kActionPreprocessMeshMapped,
        [this]
        {
            onMeshMapped();
        });
    reg(PublicConstants::kActionPreprocessMeshSweep,
        [this]
        {
            onMeshSweep();
        });
    reg(PublicConstants::kActionPreprocessMeshPartition,[this]() { addSmeshPartition(); });

    reg(PublicConstants::kActionPreprocessMeshEdit,
        [this]
        {
            onMeshEdit();
        });
    reg(PublicConstants::kActionPreprocessMeshDisplayPanel,
        [this]
        {
            // 后处理等场景只需要打开筛选面板，不切换模型树当前节点，
            // 避免把网格 Actor 重新显示到当前结果上。
            showGridDisplayControlPanel();
        });
    reg(PublicConstants::kActionPreprocessBarElements,
        [this]
        {
            onBarElements();
        });
    reg(PublicConstants::kActionPreprocessMeshGrouping,
        [this]
        {
            onMeshGrouping();
        });

    reg(PublicConstants::kActionPreprocessQualityStd,
        [this]
        {
            onQualityStd();
        });
}

// -------------------- handlers --------------------
void MeshModuleController::onImportGrid()
{
    openImportSettingsPage(true);
    QTimer::singleShot(0, this, &MeshModuleController::onImportGridFileDialog);
}

void MeshModuleController::onImportGridFileDialog()
{
    QString fileName;
    QFileDialog dialog(nullptr, QString("选择网格文件"));
    dialog.setNameFilter(
        QString("网格文件 (*.nas *.nss *.inp *.flac3d *.f3grid *.DAT *.vtk)"));
    dialog.setFilter(QDir::Files);
    if (dialog.exec() == QDialog::Accepted)
    {
        fileName = dialog.selectedFiles().first();
    }
    if (fileName.isEmpty())
        return;

    importGridFile(fileName);
}

void MeshModuleController::importGridFile(const QString& fileName)
{
    if (fileName.isEmpty())
        return;

    QFileInfo fi(fileName);
    const QString ext = fi.suffix().toLower();

    QString err;
    vtkSmartPointer<vtkUnstructuredGrid> grid;

    if (ext == "nas" || ext == "nss")
    {
        grid = MeshGridIO::instance().importNastran(fileName, &err);
    }
    else if (ext == "flac3d" || ext == "f3grid")
    {
        grid = MeshGridIO::instance().importFlac3D(fileName, &err);
    }
    else if (ext == "inp")
    {
        grid = MeshGridIO::instance().importInp(fileName, &err);
    }
    else if (ext == "dat")
    {
        grid = MeshGridIO::instance().loadIiinFile(fileName, &err);
    }
    else if (ext == "vtk")
    {
        grid = readVtkUnstructuredGrid(fileName, &err);
    }
    else
    {
        QMessageBox::warning(nullptr, QString("不支持的格式"),
                             QString("当前只支持 .nas / .nss / .inp / .flac3d / .f3grid / .dat / .vtk"));
        return;
    }

    if (!grid || grid->GetNumberOfCells() == 0)
    {
        QMessageBox::warning(nullptr, QString("导入失败"),
                             err.isEmpty() ? QString("网格导入失败") : err);
        return;
    }

    m_importedGrid = grid;
    normalizeRodElementLayers(m_importedGrid);
    m_hasGrid = true;
    m_sourceFileName = fileName;

    if (ext == "vtk")
    {
        // 普通 VTK 不携带 mesh_map.json，必须清掉上一张网格的分组映射。
        // 有有效 src_part 时按 SMESH 区域处理，否则按 DAT 风格的 MATYPE/LAYER 处理。
        MshPickMap vtkPickMap;
        bool hasPositiveRegion = false;
        vtkDataArray* regionArray = m_importedGrid->GetCellData()
            ? m_importedGrid->GetCellData()->GetArray("src_part")
            : nullptr;
        for (vtkIdType cid = 0; regionArray && cid < regionArray->GetNumberOfTuples(); ++cid)
        {
            if (static_cast<int>(regionArray->GetTuple1(cid)) > 0)
            {
                hasPositiveRegion = true;
                break;
            }
        }
        vtkPickMap.materialGroupCategory =
            hasPositiveRegion ? QStringLiteral("REGION") : QStringLiteral("MATYPE");
        vtkPickMap.excavationGroupCategory =
            hasPositiveRegion ? QStringLiteral("REGION") : QStringLiteral("LAYER");
        MeshGridIO::instance().setMeshMap(vtkPickMap);
    }

    if (ext == "dat")
        m_verticalAxis = MeshGridIO::verticalAxisOfGrid(m_importedGrid, m_verticalAxis);
    else
        MeshGridIO::setVerticalAxis(m_importedGrid, m_verticalAxis);

    if (m_importSettingsWidget)
    {
        m_importSettingsWidget->setVerticalAxis(m_verticalAxis);
        m_importSettingsWidget->setSourceFileName(m_sourceFileName);
    }

    {
        vtkSmartPointer<vtkUnstructuredGrid> tmpLine;
        vtkSmartPointer<vtkUnstructuredGrid> tmpNonLine;
        splitGridByDim(m_importedGrid, tmpLine, tmpNonLine);
    }

    if (ext == "dat")
    {
        const QString projectDir =
            PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
        if (!projectDir.trimmed().isEmpty())
        {
            QDir inputDir(SolverFilePaths::projectInputDirectory(projectDir));
            if (!QDir().mkpath(inputDir.absolutePath()))
            {
                QMessageBox::warning(nullptr, QString("DAT 备份失败"),
                                     QString("无法创建计算输入目录，未能保存网格计算数据。"));
            }
            else
            {
                const QString targetIiin = inputDir.filePath(QStringLiteral("IIIN.DAT"));
                if (QFile::exists(targetIiin) && !QFile::remove(targetIiin))
                {
                    QMessageBox::warning(nullptr, QString("DAT 备份失败"),
                                         QString("无法覆盖已有网格计算数据。"));
                }
                else if (!QFile::copy(fileName, targetIiin))
                {
                    QMessageBox::warning(nullptr, QString("DAT 备份失败"),
                                         QString("导入后保存网格计算数据失败。"));
                }
            }
        }
    }

    syncPickMapGroupingFromGrid(m_importedGrid);

    m_splitCacheSource = nullptr;
    m_splitCacheLine = nullptr;
    m_splitCacheNonLine = nullptr;

    // Multiple source regions should be visible even when they share MATYPE.
    meshDisplayPreferSourceColors(m_importedGrid);
    displayCurrentGrid(true);
    openGridDisplayControlPage();
    updateQualityCheckContext(true);

    PickManager::instance().clearDomainSelection(QStringLiteral("MaterialManager"));
}

bool MeshModuleController::importGeneratedSmeshMesh(const QString& vtkPath, QString* errMsg)
{
    QString err;
    auto grid = readVtkUnstructuredGrid(vtkPath, &err);
    if (!grid || grid->GetNumberOfCells() == 0)
    {
        if (errMsg)
            *errMsg = err.isEmpty() ? QStringLiteral("生成的网格文件为空。") : err;
        return false;
    }

    m_importedGrid = grid;
    m_hasGrid = true;
    m_sourceFileName = vtkPath;

    // A generated SMESH VTK is a new mesh, not a reload of the previous
    // previous imported mesh. Keeping the old map here left stale Physical ids,
    // boundary points and cell ids behind, so UI combo boxes targeted data
    // from the preceding mesh.
    MshPickMap smeshPickMap;
    smeshPickMap.materialGroupCategory = QStringLiteral("REGION");
    smeshPickMap.excavationGroupCategory = QStringLiteral("REGION");
    MeshGridIO::instance().setMeshMap(smeshPickMap);
    {
        vtkSmartPointer<vtkUnstructuredGrid> tmpLine;
        vtkSmartPointer<vtkUnstructuredGrid> tmpNonLine;
        splitGridByDim(m_importedGrid, tmpLine, tmpNonLine);
    }

    syncPickMapGroupingFromGrid(m_importedGrid);
    meshDisplayPreferSourceColors(m_importedGrid);
    displayCurrentGrid(true);
    openGridDisplayControlPage();
    updateQualityCheckContext(true);
    PickManager::instance().clearDomainSelection(QStringLiteral("MaterialManager"));
    return true;
}
MshPickMap MeshModuleController::mshPickMapSolid()
{
    // 仅保留兼容旧页面的出口；不再在这里生成/修正边界规则。
    // 规则统一在 MeshGridIO::normalizePhysicalTagSets() 中完成。
    return MeshGridIO::instance().getMeshMap();
}

QString MeshModuleController::matypePhysicalName(int matype, int dim) const
{
    if (matype <= 0)
    {
        return QString();
    }
    return MeshGridIO::instance().getMeshMapRef().physicalName(matype, dim);
}

void MeshModuleController::setMshPickMapSolid(const MshPickMap& pickMap)
{
    // 统一入口交给 MeshGridIO::setMeshMap() 规整，Controller 不再维护第二套边界规则。
    MeshGridIO::instance().setMeshMap(pickMap);
}

void MeshModuleController::onExportGrid()
{
    if (!m_hasGrid || !m_importedGrid)
    {
        QMessageBox::warning(nullptr, QString("无可导出的网格"), QString("请先导入网格后再导出。"));
        return;
    }

    QString defaultPath = m_sourceFileName.isEmpty()
                              ? QString("IIIN.dat")
                              : MeshGridIO::defaultIiinOutputPath(m_sourceFileName);

    QString fileName = QFileDialog::getSaveFileName(nullptr, QString("导出计算网格"), defaultPath,
                                                    QString("计算网格文件 (*.dat)"));
    if (fileName.isEmpty())
        return;

    QString err;
    if (!MeshGridIO::instance().exportIiinFile(fileName, m_importedGrid, &err, m_verticalAxis))
    {
        QMessageBox::warning(nullptr, QString("导出失败"),
                             err.isEmpty() ? QString("IIIN.DAT 导出失败") : err);
        return;
    }
}

void MeshModuleController::onMeshGlobal()
{
    if(!m_gridNode)return;
    if (auto* existed = findDirectChildByKind(m_gridNode, "GlobalMeshing"))
    {
        if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
            pm->setCurrentItem(existed);
        return;
    }
    MeshGlobalMeshingWidget* w = new MeshGlobalMeshingWidget();
    auto* node = new ProjectNode("整体剖分", QUuid::createUuid().toString(), (BasePage*)w);
    node->setData(0, ProjectNode::RoleModule, "Mesh");
    node->setData(0, ProjectNode::RoleKind, "GlobalMeshing");
    node->setData(0, ProjectNode::RoleUuid, node->getUuid());
    m_gridNode->addChild(node);
    m_gridNode->setExpanded(true);
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(node);
}

void MeshModuleController::onMeshSingle() { openLocalMeshSettings(3); }
void MeshModuleController::onMeshLocalFace() { openLocalMeshSettings(2); }
void MeshModuleController::onMeshLocalEdge() { openLocalMeshSettings(1); }

void MeshModuleController::openLocalMeshSettings(int dimension)
{
    if(!m_gridNode)return;
    auto* global=findDirectChildByKind(m_gridNode,"GlobalMeshing");
    if(!global){onMeshGlobal();global=findDirectChildByKind(m_gridNode,"GlobalMeshing");}
    auto* page=global?dynamic_cast<MeshGlobalMeshingWidget*>(global->getPage()):nullptr;
    if(!page)return;
    if(dimension==3 && page->meshingDimension()!=3) {
        QMessageBox::warning(nullptr,tr("体网格设置"),tr("当前为二维表面网格，请在整体剖分中选择三维体网格。"));
        return;
    }
    addSmeshSubmesh(dimension,global->getUuid());
}

void MeshModuleController::onMeshMapped()
{
    // auto* w = new MeshMappedMeshingWidget();
    auto w = std::make_unique<QWidget>();
}

void MeshModuleController::onMeshSweep() { addSmeshSweep(); }

void MeshModuleController::onQualityStd()
{
    if (!m_gridNode)
    {
        QMessageBox::warning(nullptr, "网格质量检查", "请先新建或打开工程。");
        return;
    }

    if (auto* existed = findDirectChildByKind(m_gridNode, QStringLiteral("MeshQualityCheck")))
    {
        m_qualityCheckWidget = dynamic_cast<MeshQualityCheckWidget*>(existed->getPage());
        updateQualityCheckContext(false);
        if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
            pm->setCurrentItem(existed);
        return;
    }

    auto* page = new MeshQualityCheckWidget();
    m_qualityCheckWidget = page;
    connect(page, &MeshQualityCheckWidget::checkRequested,
            this, &MeshModuleController::runQualityCheck);
    connect(page, &MeshQualityCheckWidget::stopRequested,
            this, &MeshModuleController::stopQualityCheck);

    auto* node = new ProjectNode(QStringLiteral("质量检查"),
                                 QUuid::createUuid().toString(), page);
    node->setData(0, ProjectNode::RoleModule, QStringLiteral("Mesh"));
    node->setData(0, ProjectNode::RoleKind, QStringLiteral("MeshQualityCheck"));
    m_gridNode->addChild(node);
    m_gridNode->setExpanded(true);
    updateQualityCheckContext(true);

    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(node);
}

int MeshModuleController::suggestedQualityObjectType() const
{
    const int gridObject = MeshGridIO::objectTypeOfGrid(m_importedGrid, 0);
    bool hasCave = false;
    bool hasDam = false;
    bool hasSlope = false;
    if (m_gridNode && m_gridNode->treeWidget())
    {
        QTreeWidgetItemIterator iterator(m_gridNode->treeWidget());
        while (*iterator)
        {
            auto* node = dynamic_cast<ProjectNode*>(*iterator);
            if (node && node->data(0, ProjectNode::RoleModule).toString()
                            == QStringLiteral("Solver"))
            {
                const QString kind = node->data(0, ProjectNode::RoleKind).toString();
                hasCave = hasCave || kind == QStringLiteral("CaveExcavationAnalysis");
                hasDam = hasDam || kind == QStringLiteral("DamAnalysis");
                hasSlope = hasSlope || kind == QStringLiteral("SlopeStabilityAnalysis");
            }
            ++iterator;
        }
    }

    const int detectedCount = static_cast<int>(hasCave) + static_cast<int>(hasDam)
                              + static_cast<int>(hasSlope);
    if (detectedCount == 1)
        return hasCave ? 6 : hasDam ? 8 : 7;
    if (gridObject == 6 || gridObject == 7 || gridObject == 8)
        return gridObject;
    return 6;
}

void MeshModuleController::updateQualityCheckContext(bool useSuggestedObject)
{
    if (!m_qualityCheckWidget || !m_importedGrid
        || m_importedGrid->GetNumberOfCells() <= 0)
        return;

    double rawBounds[6] = {};
    m_importedGrid->GetBounds(rawBounds);
    std::array<double, 6> bounds = {};
    std::copy(std::begin(rawBounds), std::end(rawBounds), bounds.begin());

    int dimension = 2;
    const vtkIdType cellCount = m_importedGrid->GetNumberOfCells();
    const vtkIdType sampleCount = qMin<vtkIdType>(cellCount, 512);
    for (vtkIdType i = 0; i < sampleCount; ++i)
    {
        if (auto* cell = m_importedGrid->GetCell(i))
            dimension = qMax(dimension, cell->GetCellDimension());
        if (dimension >= 3)
            break;
    }
    dimension = qBound(2, dimension, 3);

    QVector<double> positiveSpans;
    for (int axis = 0; axis < 3; ++axis)
    {
        const double span = rawBounds[axis * 2 + 1] - rawBounds[axis * 2];
        if (span > 0.0)
            positiveSpans.push_back(span);
    }
    std::sort(positiveSpans.begin(), positiveSpans.end(), std::greater<double>());
    double measure = 1.0;
    const int usedDimensions = qMin(dimension, positiveSpans.size());
    for (int i = 0; i < usedDimensions; ++i)
        measure *= positiveSpans.at(i);
    const double characteristicLength =
        usedDimensions > 0
            ? std::pow(measure / qMax<vtkIdType>(1, cellCount), 1.0 / usedDimensions)
            : 1.0;

    m_qualityCheckWidget->setMeshContext(suggestedQualityObjectType(), m_verticalAxis,
                                         cellCount, dimension, bounds,
                                         characteristicLength, useSuggestedObject);
}

void MeshModuleController::stopQualityCheck()
{
    auto* process = findChild<QProcess*>(QStringLiteral("gridQualityProcess"));
    if (!process || process->state() == QProcess::NotRunning)
        return;

    process->setProperty("qualityStopRequested", true);
    if (m_qualityCheckWidget)
        m_qualityCheckWidget->setStatus(QStringLiteral("正在停止检查。"));
    if (auto* output = ServiceRegistry::instance().getService<OutputService>())
        output->outputLog(QStringLiteral("正在停止网格质量检查"));
    process->kill();
}

void MeshModuleController::runQualityCheck()
{
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();

    if (projectDir.isEmpty())
    {
        QMessageBox::warning(nullptr, "网格质量检查", "当前未找到工程目录，无法执行网格质量检查。");
        return;
    }

    if (!m_hasGrid || !m_importedGrid || m_importedGrid->GetNumberOfCells() <= 0)
    {
        QMessageBox::warning(nullptr, "网格质量检查", "当前没有可检查的网格，请先导入或生成网格。");
        return;
    }

    if (auto* runningProcess = findChild<QProcess*>(QStringLiteral("gridQualityProcess"));
        runningProcess && runningProcess->state() != QProcess::NotRunning)
    {
        QMessageBox::information(nullptr, "网格质量检查", "网格质量检查正在进行，请等待本次检查完成。");
        return;
    }

    if (!m_qualityCheckWidget)
        return;
    updateQualityCheckContext(false);
    const MeshQualityCheckOptions options = m_qualityCheckWidget->options();

    QStringList unsupportedJacobianTypes;
    if (options.checkCase == 0 || options.checkCase == 15)
    {
        static const QSet<int> supportedTypes = {
            1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 17, 21,
            29, 31, 32, 33, 34, 35, 38};
        QSet<int> unsupported;
        if (auto* array = m_importedGrid->GetCellData()->GetArray("KETYPE"))
        {
            for (vtkIdType i = 0; i < array->GetNumberOfTuples(); ++i)
            {
                const int type = static_cast<int>(std::llround(array->GetComponent(i, 0)));
                if (!supportedTypes.contains(type))
                    unsupported.insert(type);
            }
        }
        QList<int> ordered = unsupported.values();
        std::sort(ordered.begin(), ordered.end());
        for (int type : ordered)
            unsupportedJacobianTypes.push_back(QString::number(type));
    }

    const QString iiidirPath = QStringLiteral("D:/IIIDIR.DAT");

    const QString projectDirNative =
        QDir::toNativeSeparators(QDir(projectDir).absolutePath());

    const QString inputDir = QDir::toNativeSeparators(
        QDir(projectDir).absoluteFilePath(QStringLiteral("工程数据/网格质量检查")));

    if (!QDir().mkpath(inputDir))
    {
        QMessageBox::warning(nullptr, "网格质量检查",
                             QString("无法创建网格质量检查数据目录：\n%1").arg(inputDir));
        return;
    }

    const QString iiinPath = QDir(inputDir).filePath(QStringLiteral("IIIN.DAT"));
    QString exportError;
    MeshGridIO::setVerticalAxis(m_importedGrid, m_verticalAxis);
    if (!MeshGridIO::instance().exportIiinFile(
            iiinPath, m_importedGrid, &exportError, m_verticalAxis))
    {
        QMessageBox::warning(
            nullptr, "网格质量检查",
            exportError.isEmpty() ? QStringLiteral("无法准备当前网格的质量检查数据。")
                                  : exportError);
        return;
    }

    const QStringList staleOutputFiles = {
        QStringLiteral("III110.DAT"), QStringLiteral("KEARRAY.DAT"),
        QStringLiteral("MABAR.DAT"), QStringLiteral("MAROCK.DAT")};
    for (const QString& fileName : staleOutputFiles)
        QFile::remove(QDir(inputDir).filePath(fileName));

    const QString calculationInputDir = SolverFilePaths::projectInputDirectory(projectDir);
    const QString sourceMaterialFile =
        QDir(calculationInputDir).filePath(QStringLiteral("IMAT.DAT"));
    const QString qualityMaterialFile =
        QDir(inputDir).filePath(QStringLiteral("IMAT.DAT"));
    if (QFileInfo::exists(sourceMaterialFile))
        copyFileOverwrite(sourceMaterialFile, qualityMaterialFile);
    else
        QFile::remove(qualityMaterialFile);

    const QString gridQualityDir =
            QDir(QCoreApplication::applicationDirPath()).filePath("grid_quality");

    if (!QDir(gridQualityDir).exists())
    {
        QMessageBox::warning(nullptr, "网格质量检查",
                             QString("未找到网格质量检查程序目录:\n%1").arg(gridQualityDir));
        return;
    }

    const QString exePath =
        QDir::toNativeSeparators(
            QDir(gridQualityDir).filePath("A02_CKJACOBI.exe"));

    if (!QFileInfo::exists(exePath))
    {
        QMessageBox::warning(nullptr, "网格质量检查",
                             QString("未找到可执行文件:\n%1").arg(exePath));
        return;
    }

    const auto ensureTrailingSlash = [](QString path) -> QString
    {
        path = QDir::toNativeSeparators(path);
        if (!path.endsWith("\\") && !path.endsWith("/"))
        {
            path += "\\";
        }
        return path;
    };

    const auto writeIiidir = [iiidirPath, inputDir, ensureTrailingSlash](const QString& rootDir,
                                                                         int objectType) -> bool
    {
        const QString rootDirWithSlash = ensureTrailingSlash(rootDir);
        const QString inputDirWithSlash = ensureTrailingSlash(inputDir);

        const QString content =
            QString("'%1'\n"
                    "'1'\n"
                    "'9.81 0.0 1'\n"
                    "'D:\\NASGEWIN64\\'\n"
                    "        0  IDEVICE\n"
                    "           0  ILOG_GZKZR\n"
                    "       0  ILOG_SOLVE\n"
                    "'%2'\n"
                    "%3   \n")
                .arg(rootDirWithSlash, inputDirWithSlash)
                .arg(qBound(0, objectType, 8), 12);

        QFile file(iiidirPath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        {
            return false;
        }

        file.write(content.toLocal8Bit());
        file.close();
        return true;
    };

    const auto writeMesgItask = [](const QString& dirPath) -> bool
    {
        const QString mesgItaskPath =
            QDir(dirPath).filePath(QStringLiteral("MESG_ITASK.DAT"));

        QFile file(mesgItaskPath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        {
            return false;
        }

        file.write("2\n");
        file.close();

        return true;
    };
    QByteArray originalIiidir;
    bool hadOriginalIiidir = false;
    {
        QFile originalFile(iiidirPath);
        if (originalFile.open(QIODevice::ReadOnly))
        {
            originalIiidir = originalFile.readAll();
            hadOriginalIiidir = true;
        }
    }

    // 检查程序通过 D:/IIIDIR.DAT 找到当前工程的独立检查数据。
    if (!writeIiidir(inputDir, options.expectedObject))
    {
        QMessageBox::warning(nullptr, "网格质量检查",
                             QString("无法写入 %1").arg(iiidirPath));
        return;
    }

    // A02_CKJACOBI 从 IIIDIR.DAT 指定的数据目录读取任务号，而不是从程序目录读取。
    if (!writeMesgItask(inputDir))
    {
        if (hadOriginalIiidir)
        {
            QFile restoreFile(iiidirPath);
            if (restoreFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
                restoreFile.write(originalIiidir);
        }
        else
        {
            writeIiidir(projectDirNative, options.expectedObject);
        }

        QMessageBox::warning(nullptr, "网格质量检查",
                             QString("无法准备网格质量检查任务配置：\n%1")
                                 .arg(QDir(inputDir).filePath("MESG_ITASK.DAT")));
        return;
    }

    auto* process = new QProcess(this);
    process->setObjectName(QStringLiteral("gridQualityProcess"));
    process->setProgram(exePath);
    process->setWorkingDirectory(gridQualityDir);
    process->setProcessChannelMode(QProcess::MergedChannels);

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("FORT_BUFFERED"), QStringLiteral("FALSE"));
    process->setProcessEnvironment(environment);

#ifdef Q_OS_WIN
    process->setCreateProcessArgumentsModifier(
        [](QProcess::CreateProcessArguments* args)
        {
            args->flags |= CREATE_NO_WINDOW;
        });
#endif

    auto runtime = std::make_shared<GridQualityRuntime>();
    runtime->options = options;

    const auto outputLog = [](const QString& message) {
        const QString text = message.trimmed();
        if (text.isEmpty())
            return;
        if (auto* output = ServiceRegistry::instance().getService<OutputService>())
            output->outputLog(text);
    };

    const auto restoreIiidir = [runtime, iiidirPath, originalIiidir, hadOriginalIiidir,
                                writeIiidir, projectDirNative, options]() {
        if (runtime->iiidirRestored)
            return;
        runtime->iiidirRestored = true;

        if (hadOriginalIiidir)
        {
            QFile restoreFile(iiidirPath);
            if (restoreFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
                restoreFile.write(originalIiidir);
            return;
        }
        writeIiidir(projectDirNative, options.expectedObject);
    };

    const auto sendAnswer = [process, outputLog](const QString& answer, const QString& description) {
        if (process->state() == QProcess::NotRunning)
            return;
        if (!description.trimmed().isEmpty())
            outputLog(QStringLiteral("检查参数：%1").arg(description));
        process->write((answer + QLatin1Char('\n')).toLocal8Bit());
    };

    const auto processPrompts = [runtime, sendAnswer, process](const QString& line) {
        static const QRegularExpression stepPattern(
            QStringLiteral("ISTEP\\s*==\\s*(\\d+)"),
            QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression internalErrorPattern(
            QStringLiteral("ERROR\\s+\\d+\\s+IN|ERROR\\s+IN|UN-EXPECTED|FORRTL|SEVERE"),
            QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression elementTypePattern(
            QStringLiteral("KETYPE\\s*=\\s*(\\d+)"),
            QRegularExpression::CaseInsensitiveOption);

        const auto stepMatch = stepPattern.match(line);
        if (stepMatch.hasMatch())
            runtime->highestStep = qMax(runtime->highestStep,
                                        stepMatch.captured(1).toInt());
        if (internalErrorPattern.match(line).hasMatch())
            runtime->reportedError = true;
        const auto elementTypeMatch = elementTypePattern.match(line);
        if (elementTypeMatch.hasMatch())
            runtime->unsupportedElementType = elementTypeMatch.captured(1).toInt();

        const auto sendOnce = [runtime, &line, &sendAnswer](const QString& key,
                                                            const QString& prompt,
                                                            const QString& answer,
                                                            const QString& description) {
            if (runtime->answeredPrompts.contains(key) || !line.contains(prompt))
                return;
            runtime->answeredPrompts.insert(key);
            sendAnswer(answer, description);
        };

        sendOnce(QStringLiteral("object"), QStringLiteral("需要改变网格文件中的研究对象类型吗"),
                 runtime->options.repairObject ? QStringLiteral("1") : QStringLiteral("0"),
                 runtime->options.repairObject ? QStringLiteral("修正研究对象")
                                                : QStringLiteral("不修改研究对象"));
        sendOnce(QStringLiteral("vertical"), QStringLiteral("需要改变网格文件中的垂直向轴号吗"),
                 QString::number(runtime->options.repairVerticalAxis),
                 runtime->options.repairVerticalAxis == 3 ? QStringLiteral("垂直轴修正为 Z 轴")
                                                          : runtime->options.repairVerticalAxis == 2
                                                                ? QStringLiteral("垂直轴修正为 Y 轴")
                                                                : QStringLiteral("不修改垂直轴"));
        sendOnce(QStringLiteral("attributes"), QStringLiteral("需要修正属性吗"),
                 runtime->options.repairAttributes ? QStringLiteral("1") : QStringLiteral("0"),
                 runtime->options.repairAttributes ? QStringLiteral("清零异常单元属性")
                                                   : QStringLiteral("不修改单元属性"));
        sendOnce(QStringLiteral("minimumDistance"), QStringLiteral("节点间最小距离是多少"),
                 QString::number(runtime->options.minimumNodeDistance, 'g', 12),
                 QStringLiteral("节点最小距离 = %1")
                     .arg(runtime->options.minimumNodeDistance, 0, 'g', 12));
        sendOnce(QStringLiteral("replaceNodes"), QStringLiteral("需要节点替换吗"),
                 runtime->options.replaceNearbyNodes ? QStringLiteral("1") : QStringLiteral("0"),
                 runtime->options.replaceNearbyNodes ? QStringLiteral("合并距离过近的节点")
                                                     : QStringLiteral("不合并距离过近的节点"));
        sendOnce(QStringLiteral("boundaryTolerance"), QStringLiteral("边界节点坐标值差值容限"),
                 QString::number(runtime->options.boundaryTolerance, 'g', 12),
                 QStringLiteral("边界坐标容限 = %1")
                     .arg(runtime->options.boundaryTolerance, 0, 'g', 12));
        sendOnce(QStringLiteral("jacobianTolerance"), QStringLiteral("雅可比行列式容限值"),
                 QString::number(runtime->options.jacobianTolerance, 'g', 12),
                 QStringLiteral("雅可比行列式容限 = %1")
                     .arg(runtime->options.jacobianTolerance, 0, 'g', 12));
        sendOnce(QStringLiteral("jacobianStart"), QStringLiteral("雅可比检查的起始单元"),
                 QString::number(runtime->options.jacobianStartElement),
                 QStringLiteral("雅可比检查起始单元 = %1")
                     .arg(runtime->options.jacobianStartElement));
        sendOnce(QStringLiteral("aspectRatio"), QStringLiteral("最大长细比容限值"),
                 QString::number(runtime->options.maximumAspectRatio, 'g', 12),
                 QStringLiteral("最大长细比 = %1")
                     .arg(runtime->options.maximumAspectRatio, 0, 'g', 12));

        if (line.contains(QStringLiteral("这个值有问题吗")))
        {
            sendAnswer(runtime->options.useSpecifiedBounds ? QStringLiteral("1")
                                                            : QStringLiteral("0"),
                       runtime->options.useSpecifiedBounds
                           ? QStringLiteral("使用指定模型边界")
                           : QStringLiteral("按当前模型边界归整临近节点"));
        }

        const QStringList boundPromptKeys = {
            QStringLiteral("X方向最小值"), QStringLiteral("X方向最大值"),
            QStringLiteral("Y方向最小值"), QStringLiteral("Y方向最大值"),
            QStringLiteral("Z方向最小值"), QStringLiteral("Z方向最大值")};
        for (int i = 0; i < boundPromptKeys.size(); ++i)
        {
            const QString& boundKey = boundPromptKeys.at(i);
            if (line.contains(boundKey + QStringLiteral("期望值")))
            {
                sendOnce(QStringLiteral("bound%1").arg(i),
                         boundKey + QStringLiteral("期望值"),
                         QString::number(runtime->options.bounds[i], 'g', 15),
                         QStringLiteral("%1 = %2")
                             .arg(boundKey, QString::number(runtime->options.bounds[i], 'g', 15)));
            }
        }

        const QString upperLine = line.toUpper();
        if (!runtime->finalAnswerSent
            && (line.contains(QStringLiteral("输入任意键，结束进程"))
                || upperLine.contains(QStringLiteral("INPUT ANY CHAR"))
                || upperLine.contains(QStringLiteral("PRESS ANY"))))
        {
            runtime->finalAnswerSent = true;
            sendAnswer(QStringLiteral("0"), QString());
            process->closeWriteChannel();
        }
    };

    const auto consumeOutput = [runtime, processPrompts, outputLog](const QByteArray& bytes,
                                                                    bool flushRemainder) {
        runtime->outputBytes.append(bytes);
        QStringList decodedLines;
        for (;;)
        {
            const qsizetype newline = runtime->outputBytes.indexOf('\n');
            if (newline < 0)
                break;
            QByteArray lineBytes = runtime->outputBytes.left(newline);
            runtime->outputBytes.remove(0, newline + 1);
            if (lineBytes.endsWith('\r'))
                lineBytes.chop(1);
            const QString line = QString::fromLocal8Bit(lineBytes);
            decodedLines.push_back(line);
        }

        if (flushRemainder && !runtime->outputBytes.isEmpty())
        {
            const QString line = QString::fromLocal8Bit(runtime->outputBytes);
            runtime->outputBytes.clear();
            decodedLines.push_back(line);
        }

        if (!decodedLines.isEmpty())
        {
            outputLog(decodedLines.join(QLatin1Char('\n')));
            for (const QString& line : decodedLines)
                processPrompts(line);
        }
    };

    connect(process, &QProcess::readyReadStandardOutput, this, [process, consumeOutput]() {
        consumeOutput(process->readAllStandardOutput(), false);
    });

    connect(process,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this,
            [this, process, runtime, restoreIiidir, consumeOutput, outputLog, iiinPath, inputDir]
            (int exitCode, QProcess::ExitStatus exitStatus)
            {
                consumeOutput(process->readAllStandardOutput(), true);
                restoreIiidir();

                if (process->property("qualityStopRequested").toBool())
                {
                    outputLog(QStringLiteral("网格质量检查已停止"));
                    if (m_qualityCheckWidget)
                    {
                        m_qualityCheckWidget->setRunning(false);
                        m_qualityCheckWidget->setStatus(QStringLiteral("检查已停止。"));
                    }
                    process->deleteLater();
                    return;
                }

                const bool processSucceeded =
                    exitStatus == QProcess::NormalExit && exitCode == 0;
                const bool fullCheckCompleted =
                    runtime->options.checkCase != 0 || runtime->highestStep >= 19;
                const bool checkCompleted = processSucceeded && !runtime->reportedError
                                            && fullCheckCompleted;

                if (processSucceeded && !runtime->reportedError
                    && runtime->options.applyCorrections)
                {
                    QString reloadError;
                    auto checkedGrid = MeshGridIO::instance().loadIiinFile(iiinPath, &reloadError);
                    if (checkedGrid && checkedGrid->GetNumberOfCells() > 0)
                    {
                        m_importedGrid = checkedGrid;
                        m_hasGrid = true;
                        m_verticalAxis = MeshGridIO::verticalAxisOfGrid(m_importedGrid, m_verticalAxis);
                        if (m_importSettingsWidget)
                            m_importSettingsWidget->setVerticalAxis(m_verticalAxis);
                        if (m_gridEditingWidget)
                            m_gridEditingWidget->setEditableGrid(m_importedGrid, m_sourceFileName);
                        syncPickMapGroupingFromGrid(m_importedGrid);
                        m_splitCacheSource = nullptr;
                        m_splitCacheLine = nullptr;
                        m_splitCacheNonLine = nullptr;
                        meshDisplayPreferSourceColors(m_importedGrid);
                        displayCurrentGrid();
                        PickManager::instance().clearDomainSelection(
                            QStringLiteral("MaterialManager"));
                        updateQualityCheckContext(false);
                        outputLog(QStringLiteral("检查修正已应用到当前网格"));
                    }
                    else if (!reloadError.trimmed().isEmpty())
                    {
                        outputLog(QStringLiteral("检查数据重新载入失败：%1").arg(reloadError));
                    }

                }

                QString statusText;
                if (checkCompleted)
                {
                    statusText = runtime->options.checkCase == 0
                                     ? QStringLiteral("全面检查已完成。")
                                     : QStringLiteral("所选检查已完成。");
                    outputLog(QStringLiteral("网格质量检查完成"));
                }
                else if (runtime->reportedError && runtime->unsupportedElementType > 0)
                {
                    statusText = QStringLiteral(
                        "检查工具不支持单元类型 %1 的当前检查，本次结果不完整。")
                                     .arg(runtime->unsupportedElementType);
                    outputLog(QStringLiteral(
                        "网格质量检查未完成：检查工具在第 %1 项处理单元类型 %2 时停止，"
                        "不能判定为检查通过。")
                                  .arg(runtime->highestStep)
                                  .arg(runtime->unsupportedElementType));
                }
                else if (processSucceeded && !fullCheckCompleted && !runtime->reportedError)
                {
                    statusText = QStringLiteral(
                        "全面检查在第 %1 项结束，请根据日志处理问题后重新检查。")
                                     .arg(runtime->highestStep);
                    outputLog(QStringLiteral(
                        "网格质量检查未完整执行：最高执行到第 %1 项，不能判定为全面通过。")
                                  .arg(runtime->highestStep));
                }
                else if (runtime->reportedError)
                {
                    statusText = QStringLiteral("检查工具在第 %1 项发生内部错误，本次结果不完整。")
                                     .arg(runtime->highestStep);
                    outputLog(QStringLiteral(
                        "网格质量检查未完成：检查工具在第 %1 项发生内部错误。")
                                  .arg(runtime->highestStep));
                }
                else
                {
                    statusText = QStringLiteral("检查程序异常结束，退出码：%1。")
                                     .arg(exitCode);
                    outputLog(QStringLiteral("网格质量检查异常结束，退出码：%1")
                                  .arg(exitCode));
                }

                const QString reportPath =
                    QDir(inputDir).filePath(QStringLiteral("III110.DAT"));
                if (QFileInfo::exists(reportPath))
                    outputLog(QStringLiteral("检查报告：%1")
                                  .arg(QDir::toNativeSeparators(reportPath)));
                if (m_qualityCheckWidget)
                {
                    m_qualityCheckWidget->setRunning(false);
                    m_qualityCheckWidget->setStatus(statusText, !checkCompleted);
                }

                process->deleteLater();
            });

    connect(process,
            &QProcess::errorOccurred,
            this,
            [outputLog](QProcess::ProcessError error)
            {
                outputLog(QStringLiteral("网格质量检查进程错误，错误码：%1")
                              .arg(static_cast<int>(error)));
            });

    outputLog(QStringLiteral("网格质量检查开始"));
    outputLog(QStringLiteral("研究对象：%1")
                  .arg(gridQualityObjectDisplayName(options.expectedObject)));
    outputLog(options.applyCorrections
                  ? QStringLiteral("检查方式：检查完成后将有效修正应用到当前网格")
                  : QStringLiteral("检查方式：仅检查独立副本，不修改当前网格"));
    if (!unsupportedJacobianTypes.isEmpty())
    {
        outputLog(QStringLiteral(
            "检查提示：当前网格包含单元类型 %1，现有质量检查工具未实现这些类型的"
            "雅可比检查，第 15 项可能提前停止。")
                      .arg(unsupportedJacobianTypes.join(QStringLiteral("、"))));
    }
    m_qualityCheckWidget->setRunning(true);
    process->start();

    if (!process->waitForStarted(3000))
    {
        restoreIiidir();

        const QString errorText = process->errorString().trimmed();
        outputLog(QStringLiteral("网格质量检查启动失败：%1").arg(errorText));
        m_qualityCheckWidget->setRunning(false);
        m_qualityCheckWidget->setStatus(QStringLiteral("检查程序启动失败。"), true);
        QMessageBox::warning(nullptr, "网格质量检查", "网格质量检查程序启动失败。");

        process->deleteLater();
        return;
    }

    sendAnswer(QString::number(options.checkCase),
               QStringLiteral("检查项目 = %1").arg(gridQualityCaseDisplayName(options.checkCase)));
}

void MeshModuleController::onMeshGrouping()
{
    if (auto* existed = findDirectChildByKind(m_gridNode, "MeshGrouping"))
    {
        if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
            pm->setCurrentItem(existed);
        QMessageBox::information(nullptr, "网格分组", "网格分组节点已存在。", QMessageBox::Ok);
        return;
    }

    auto* w = new MeshGroupingWidget();
    auto* node = new ProjectNode("网格分组", QUuid::createUuid().toString(), w);
    node->setData(0, ProjectNode::RoleModule, "Mesh");
    node->setData(0, ProjectNode::RoleKind, "MeshGrouping");
    m_gridNode->addChild(node);
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(node);
}

void MeshModuleController::openImportSettingsPage(bool setCurrent)
{
    if (!m_gridNode)
        return;

    if (!m_importSettingsWidget)
    {
        m_importSettingsWidget = new MeshImportSettingsWidget();
        m_importSettingsWidget->setVerticalAxis(m_verticalAxis);
        m_importSettingsWidget->setSourceFileName(m_sourceFileName);
        connect(m_importSettingsWidget, &MeshImportSettingsWidget::importRequested, this,
                &MeshModuleController::onImportGridFileDialog);
        connect(m_importSettingsWidget, &MeshImportSettingsWidget::verticalAxisChanged, this,
                [this](int axis)
                {
                    if (axis < 1 || axis > 3)
                        return;
                    m_verticalAxis = axis;
                    MeshGridIO::setVerticalAxis(m_importedGrid, m_verticalAxis);
                });
        m_gridNode->setPage(m_importSettingsWidget);
    }
    else
    {
        m_importSettingsWidget->setVerticalAxis(m_verticalAxis);
        m_importSettingsWidget->setSourceFileName(m_sourceFileName);
        if (!m_gridNode->getPage())
            m_gridNode->setPage(m_importSettingsWidget);
    }

    if (setCurrent)
    {
        if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
            pm->setCurrentItem(m_gridNode);
    }
}


void MeshModuleController::onMeshEdit()
{
    openGridDisplayControlPage();
}

void MeshModuleController::ensureGridDisplayControlPanel()
{
    if (!m_gridEditingWidget)
    {
        m_gridEditingWidget = new GridEditingWidget();
        m_gridEditingWidget->setProperty("projectTreeDockPanel", true);
        m_gridEditingWidget->setProperty("visualControlPanel", true);
        // 这是辅助控制面板，不应因为树节点属于 Mesh 就强制切换当前场景。
        m_gridEditingWidget->setProperty("preserveSceneOnActivation", true);
    }

    if (m_gridEditingWidget->property("uiFrameDockWidget").isValid())
        return;

    if (auto* uiFrame = ServiceRegistry::instance().getService<UiFrameService>())
    {
        uiFrame->addWidget(m_gridEditingWidget, QStringLiteral("显示控制"));
        uiFrame->setWidgetVisible(m_gridEditingWidget, false);
    }
}

void MeshModuleController::showGridDisplayControlPanel()
{
    ensureGridDisplayControlPanel();
    if (!m_gridEditingWidget)
        return;

    if (m_importedGrid && m_importedGrid->GetNumberOfCells() > 0)
        m_gridEditingWidget->setEditableGrid(m_importedGrid, m_sourceFileName);
    else
        m_gridEditingWidget->clearGrid();

    if (auto* uiFrame = ServiceRegistry::instance().getService<UiFrameService>())
        uiFrame->setWidgetVisible(m_gridEditingWidget, true);
}

void MeshModuleController::onBarElements()
{
    if (!m_gridNode)
        return;

    // 一组锚杆布置对应一个独立工程节点。每个节点可以有自己的材料编号、
    // 几何参数和多次“添加锚杆”记录，避免所有锚杆挤在同一个页面里。
    QSet<QString> existingTitles;
    for (int i = 0; i < m_gridNode->childCount(); ++i)
    {
        auto* child = dynamic_cast<ProjectNode*>(m_gridNode->child(i));
        if (!child)
            continue;
        if (child->data(0, ProjectNode::RoleKind).toString() ==
            QStringLiteral("BarElementPlacement"))
        {
            existingTitles.insert(child->text(0));
        }
    }
    int placementIndex = 1;
    while (existingTitles.contains(QStringLiteral("锚杆布置 %1").arg(placementIndex)))
        ++placementIndex;

    auto* page = new BarElementPlacementWidget();
    m_barElementWidget = page;
    const QString uuid = QUuid::createUuid().toString();
    auto* node = new ProjectNode(QStringLiteral("锚杆布置 %1").arg(placementIndex),
                                 uuid, page);
    node->setData(0, ProjectNode::RoleUuid, uuid);
    node->setData(0, ProjectNode::RoleModule, QStringLiteral("Mesh"));
    node->setData(0, ProjectNode::RoleKind, QStringLiteral("BarElementPlacement"));
    m_gridNode->addChild(node);
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(node);
}

void MeshModuleController::openGridDisplayControlPage()
{
    if (!m_gridNode)
        return;

    ensureGridDisplayControlPanel();

    auto activatePanelNode = [this](ProjectNode* node)
    {
        if (!node)
            return;
        QTreeWidget* tree = node->treeWidget();
        const bool alreadyCurrent = tree && tree->currentItem() == node;
        auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>();
        if (pm)
            pm->setCurrentItem(node);
        if (!pm || !tree || alreadyCurrent)
            showGridDisplayControlPanel();
    };

    if (auto* existed = findDirectChildByKind(m_gridNode, "MeshEdit"))
    {
        activatePanelNode(existed);
        return;
    }

    auto* w = m_gridEditingWidget;

    auto* node = new ProjectNode("显示控制", QUuid::createUuid().toString(), w);
    node->setData(0, ProjectNode::RoleModule, "Mesh");
    node->setData(0, ProjectNode::RoleKind, "MeshEdit");
    m_gridNode->addChild(node);
    activatePanelNode(node);
}

void MeshModuleController::displayCurrentGrid(bool fitView)
{
    const qint64 rev = PublicVariables::instance().get("pick_structure_revision").toLongLong();
    PublicVariables::instance().set("pick_structure_revision", rev + 1, "project_dir");

    if (m_importedGrid)
        normalizeGridAttributeTupleCounts(m_importedGrid);

    if (!m_visualWidgetService || !m_hasGrid || !m_importedGrid)
        return;

    static const QString kMeshSurfId = QString("mesh.imported.surface");
    static const QString kMeshLineId = QString("mesh.imported.line");
    m_visualWidgetService->removeProp(kMeshSurfId, false);
    m_visualWidgetService->removeProp(kMeshLineId, false);

           // 这里不再先 splitGridByDim / vtkExtractCells 再抽 surface。
           // surface/line 的真实输入由 meshDisplayApplyGlobalState 一次性从 m_importedGrid 构造，
           // 避免导入阶段重复拷贝网格和重复构造外表面。
    auto emptySurface = vtkSmartPointer<vtkPolyData>::New();
    auto surfaceMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    surfaceMapper->SetInputData(emptySurface);
    surfaceMapper->ScalarVisibilityOff();

    auto surfActor = vtkSmartPointer<vtkActor>::New();
    surfActor->SetMapper(surfaceMapper);
    auto* surfProp = surfActor->GetProperty();
    surfProp->SetRepresentationToSurface();
    surfProp->SetColor(0.78, 0.78, 0.78);
    surfProp->EdgeVisibilityOn();
    surfProp->SetEdgeColor(0.0, 0.0, 0.0);
    surfProp->SetLineWidth(1.0);
    // 当前 Mapper 还是空输入，不能在 addProp 阶段提前重置相机。
    m_visualWidgetService->addProp(kMeshSurfId, surfActor, QString("网格"),
                                   QString("导入网格-面/体"), false, false);

    auto emptyLine = vtkSmartPointer<vtkUnstructuredGrid>::New();
    auto lineMapper = vtkSmartPointer<vtkDataSetMapper>::New();
    lineMapper->SetInputData(emptyLine);
    lineMapper->ScalarVisibilityOff();

    auto lineActor = vtkSmartPointer<vtkActor>::New();
    lineActor->SetMapper(lineMapper);
    auto* lineProp = lineActor->GetProperty();
    lineProp->SetColor(0.78, 0.78, 0.78);
    lineProp->SetLineWidth(2.0);
    lineProp->SetSpecular(0.0);
    lineProp->RenderLinesAsTubesOn();
    m_visualWidgetService->addProp(kMeshLineId, lineActor, QString("网格"),
                                   QString("导入网格-线"), false, false);

    m_previewSurfaceGrid = m_importedGrid;
    m_previewSurfaceActor = surfActor;
    m_previewSurfaceMapper = surfaceMapper;
    m_previewLineGrid = emptyLine;
    m_previewLineActor = lineActor;
    m_previewLineMapper = lineMapper;

    m_previewGrid = m_previewSurfaceGrid;
    m_previewActor = m_previewSurfaceActor;
    m_previewMapper = m_previewSurfaceMapper;

           // 网格导入/预览后默认只显示网格。
    m_visualWidgetService->setGroupVisible("Geometry", false, false);

           // 全局显示控制在这里统一应用。该函数会从 m_importedGrid 一次性构造：
           // 1) 只包含外表面/必要内部分界面的 surface；
           // 2) 带 vtkOriginalCellIds / vtkOriginalFaceIds / vtkOriginalFaceKeys 的拾取映射；
           // 3) 线单元显示网格。
    meshDisplayApplyGlobalState(m_importedGrid,
                                m_previewSurfaceActor,
                                m_previewSurfaceMapper,
                                m_previewLineActor,
                                m_previewLineMapper,
                                m_loadedSurfaceDisplayCache.GetPointer(),
                                m_loadedLineDisplayCache.GetPointer());

    // 缓存只服务于工程恢复的第一次显示。后续网格编辑/筛选必须基于实时网格重建。
    m_loadedSurfaceDisplayCache = nullptr;
    m_loadedLineDisplayCache = nullptr;

    if (auto* dsMapper = vtkDataSetMapper::SafeDownCast(m_previewLineMapper))
        m_previewLineGrid = vtkUnstructuredGrid::SafeDownCast(dsMapper->GetInput());

           // 必须在网格 prop 完成更新后再同步 PickManager 结构，否则会抓到旧 actor/mapper。
    PickManager::instance().updateStructure();

    // 仅在导入、生成或工程恢复时自动适应；普通显示刷新保留当前视角。
    if (fitView)
        m_visualWidgetService->resetCamera(true);
    else
        m_visualWidgetService->render();
}

vtkSmartPointer<vtkUnstructuredGrid> MeshModuleController::getVtkUnstructuredGrid()
{
    return m_importedGrid;
}

QString MeshModuleController::sourceMeshFilePath() const
{
    return m_sourceFileName;
}

int MeshModuleController::verticalAxis() const
{
    return m_verticalAxis;
}

bool MeshModuleController::exportIiinDatFile(const QString& filePath, QString* errMsg)
{
    if (!m_hasGrid || !m_importedGrid)
    {
        if (errMsg)
            *errMsg = QStringLiteral("当前无可导出的网格数据");
        return false;
    }
    MeshGridIO::setVerticalAxis(m_importedGrid, m_verticalAxis);
    return MeshGridIO::instance().exportIiinFile(filePath, m_importedGrid, errMsg, m_verticalAxis);
}

void MeshModuleController::splitGridByDim(vtkUnstructuredGrid* src,
                                          vtkSmartPointer<vtkUnstructuredGrid>& outLine,
                                          vtkSmartPointer<vtkUnstructuredGrid>& outNonLine)
{
    outLine = nullptr;
    outNonLine = nullptr;
    if (!src)
        return;

    if (m_splitCacheSource == src && m_splitCacheNonLine)
    {
        outLine = m_splitCacheLine;
        outNonLine = m_splitCacheNonLine;
        return;
    }

    auto lineIds = vtkSmartPointer<vtkIdList>::New();
    auto nonLineIds = vtkSmartPointer<vtkIdList>::New();

    const vtkIdType nCells = src->GetNumberOfCells();
    for (vtkIdType cid = 0; cid < nCells; ++cid)
    {
        vtkCell* c = src->GetCell(cid);
        if (!c)
            continue;

        const int dim = meshDisplayCellDimensionFast(src, cid);
        if (dim == 1)
        {
            lineIds->InsertNextId(cid);
        }
        else
        {
            nonLineIds->InsertNextId(cid);
        }
    }

    auto sourceWithIds = meshDisplayCopyWithFullCellIds(src);

    if (lineIds->GetNumberOfIds() > 0)
    {
        auto ex = vtkSmartPointer<vtkExtractCells>::New();
        ex->SetInputData(sourceWithIds);
        ex->SetCellList(lineIds);
        ex->Update();
        outLine = vtkUnstructuredGrid::SafeDownCast(ex->GetOutput());
    }

    if (nonLineIds->GetNumberOfIds() > 0)
    {
        auto ex = vtkSmartPointer<vtkExtractCells>::New();
        ex->SetInputData(sourceWithIds);
        ex->SetCellList(nonLineIds);
        ex->Update();
        outNonLine = vtkUnstructuredGrid::SafeDownCast(ex->GetOutput());
    }

           // splitGridByDim 只做维度拆分，不再追加 MATYPE/LAYER/Physical 分组表面。
           // 分组表面属于业务选择/高亮数据，不能混入显示网格；否则打开工程后会显示重复壳。

    m_splitCacheSource = src;
    m_splitCacheLine = outLine;
    m_splitCacheNonLine = outNonLine;
}

vtkSmartPointer<vtkActor> MeshModuleController::createSurfaceActor(vtkDataSet* grid) const
{
    if (!grid)
        return nullptr;

    auto mapper = vtkSmartPointer<vtkDataSetMapper>::New();
    mapper->SetInputData(grid);
    mapper->ScalarVisibilityOff();

    auto actor = vtkSmartPointer<vtkActor>::New();
    actor->SetMapper(mapper);

    auto* p = actor->GetProperty();
    p->SetColor(0.78, 0.78, 0.78);  // 与导入网格默认色一致
    p->SetSpecular(0.0);            // 避免高光导致“发亮”
    p->EdgeVisibilityOn();          // 显示边
    p->SetEdgeColor(0.0, 0.0, 0.0); // 黑色边
    p->SetLineWidth(1.0);

    return actor;
}

vtkSmartPointer<vtkActor> MeshModuleController::createLineActor(vtkUnstructuredGrid* grid) const
{
    if (!grid)
        return nullptr;

    auto mapper = vtkSmartPointer<vtkDataSetMapper>::New();
    mapper->SetInputData(grid);
    mapper->ScalarVisibilityOff();

    auto actor = vtkSmartPointer<vtkActor>::New();
    actor->SetMapper(mapper);

    auto* p = actor->GetProperty();
    p->SetColor(0.78, 0.78, 0.78); // 与面/体网格默认颜色保持一致
    p->SetLineWidth(2.0);
    p->SetSpecular(0.0);
    p->RenderLinesAsTubesOn();

    return actor;
}

static void rebuildSmeshBoundaryTagSets(MshPickMap& pick,
                                         vtkUnstructuredGrid* grid,
                                         int meshDim)
{
    if (!grid || !grid->GetFieldData() || meshDim < 2 || meshDim > 3)
        return;

    auto* nativeFacets = vtkIntArray::SafeDownCast(
        grid->GetFieldData()->GetArray("SmeshBoundaryFacets"));
    if (!nativeFacets || nativeFacets->GetNumberOfComponents() < 4)
        return;

    // The VTK field data is authoritative for a newly generated SMESH mesh.
    // Remove only old SMESH sets; imported Physical groups remain a separate model.
    QVector<MeshTagKey> removeKeys;
    for (auto it = pick.tagSets.constBegin(); it != pick.tagSets.constEnd(); ++it)
    {
        if (it.key().category.compare(QStringLiteral("SmeshBoundary"),
                                      Qt::CaseInsensitive) == 0)
            removeKeys.push_back(it.key());
    }
    for (const MeshTagKey& key : removeKeys)
        pick.tagSets.remove(key);
    for (auto it = pick.pointIdToBoundaryTags.begin();
         it != pick.pointIdToBoundaryTags.end(); ++it)
    {
        QVector<MeshTagKey>& tags = it.value();
        tags.erase(std::remove_if(tags.begin(), tags.end(), [](const MeshTagKey& key)
        {
            return key.category.compare(QStringLiteral("SmeshBoundary"),
                                        Qt::CaseInsensitive) == 0;
        }), tags.end());
    }

    const auto regionKey = [](int dimension, int id)
    {
        return QStringLiteral("%1:%2").arg(dimension).arg(id);
    };

    QHash<QString, QString> names;
    if (auto* nameArray = vtkStringArray::SafeDownCast(
            grid->GetFieldData()->GetAbstractArray("SmeshBoundaryNames")))
    {
        const vtkIdType valueCount = nameArray->GetNumberOfValues();
        for (vtkIdType value = 0; value + 2 < valueCount; value += 3)
        {
            bool dimensionOk = false;
            bool idOk = false;
            const int dimension = QString::fromStdString(
                nameArray->GetValue(value)).toInt(&dimensionOk);
            const int id = QString::fromStdString(
                nameArray->GetValue(value + 1)).toInt(&idOk);
            if (dimensionOk && dimension >= 0 && dimension < meshDim && idOk && id > 0)
            {
                names.insert(regionKey(dimension, id), QString::fromStdString(
                    nameArray->GetValue(value + 2)).trimmed());
            }
        }
    }

    struct FacetRef
    {
        vtkIdType cellId = -1;
        int localId = -1;
        std::vector<vtkIdType> pointIds;
    };

    QVector<FacetRef> gridEntities[3];
    std::map<std::vector<vtkIdType>, QVector<int>> exactEntityIndices[3];
    QHash<vtkIdType, QVector<int>> entityIndicesByPoint[3];

    auto appendEntity = [&](int entityDimension,
                            vtkIdType cellId,
                            int localId,
                            vtkCell* entity)
    {
        if (entityDimension <= 0 || entityDimension >= meshDim || !entity ||
            entity->GetNumberOfPoints() <= 0)
        {
            return;
        }

        FacetRef ref;
        ref.cellId = cellId;
        ref.localId = localId;
        ref.pointIds.reserve(static_cast<std::size_t>(entity->GetNumberOfPoints()));
        for (vtkIdType i = 0; i < entity->GetNumberOfPoints(); ++i)
            ref.pointIds.push_back(entity->GetPointId(i));
        std::sort(ref.pointIds.begin(), ref.pointIds.end());

        const int index = gridEntities[entityDimension].size();
        gridEntities[entityDimension].push_back(ref);
        exactEntityIndices[entityDimension][ref.pointIds].push_back(index);
        for (vtkIdType pointId : ref.pointIds)
            entityIndicesByPoint[entityDimension][pointId].push_back(index);
    };

    for (vtkIdType cellId = 0; cellId < grid->GetNumberOfCells(); ++cellId)
    {
        vtkCell* cell = grid->GetCell(cellId);
        if (!cell || cell->GetCellDimension() != meshDim)
            continue;

        for (int localEdge = 0; localEdge < cell->GetNumberOfEdges(); ++localEdge)
            appendEntity(1, cellId, localEdge, cell->GetEdge(localEdge));
        if (meshDim == 3)
        {
            for (int localFace = 0; localFace < cell->GetNumberOfFaces(); ++localFace)
                appendEntity(2, cellId, localFace, cell->GetFace(localFace));
        }
    }

    auto ensureSet = [&](int boundaryDimension, int boundaryId) -> MeshTagSet&
    {
        const MeshTagKey key{
            QStringLiteral("SmeshBoundary"), boundaryDimension, boundaryId};
        if (!pick.tagSets.contains(key))
        {
            MeshTagSet set;
            set.key = key;
            QString name = names.value(regionKey(boundaryDimension, boundaryId)).trimmed();
            if (name.isEmpty() || name.compare(QStringLiteral("default"),
                                                Qt::CaseInsensitive) == 0)
                name = physicalUiNameByDim(boundaryDimension, boundaryId);
            set.name = name;
            set.source = QStringLiteral("SMESH");
            pick.tagSets.insert(key, set);
        }
        return pick.tagSets[key];
    };

    const int componentCount = nativeFacets->GetNumberOfComponents();
    for (vtkIdType tuple = 0; tuple < nativeFacets->GetNumberOfTuples(); ++tuple)
    {
        const int boundaryDimension = static_cast<int>(
            std::llround(nativeFacets->GetComponent(tuple, 0)));
        const int boundaryId = static_cast<int>(
            std::llround(nativeFacets->GetComponent(tuple, 1)));
        const int pointCount = static_cast<int>(
            std::llround(nativeFacets->GetComponent(tuple, 2)));
        if (boundaryDimension < 0 || boundaryDimension >= meshDim ||
            boundaryId <= 0 || pointCount <= 0 || pointCount > componentCount - 3)
            continue;

        std::vector<vtkIdType> nativePointIds;
        nativePointIds.reserve(static_cast<std::size_t>(pointCount));
        bool valid = true;
        for (int i = 0; i < pointCount; ++i)
        {
            const vtkIdType pointId = static_cast<vtkIdType>(
                std::llround(nativeFacets->GetComponent(tuple, 3 + i)));
            if (pointId < 0 || pointId >= grid->GetNumberOfPoints())
            {
                valid = false;
                break;
            }
            nativePointIds.push_back(pointId);
        }
        if (!valid)
            continue;
        std::sort(nativePointIds.begin(), nativePointIds.end());
        nativePointIds.erase(std::unique(nativePointIds.begin(), nativePointIds.end()),
                             nativePointIds.end());

        MeshTagSet& set = ensureSet(boundaryDimension, boundaryId);
        for (vtkIdType pointId : nativePointIds)
            set.pointIds.push_back(static_cast<MeshIdType>(pointId));

        if (boundaryDimension == 0)
            continue;

        QVector<int> matchingEntities =
            exactEntityIndices[boundaryDimension][nativePointIds];
        if (matchingEntities.isEmpty() && !nativePointIds.empty())
        {
            // Tet4 -> Hex8 splits a native edge or triangle into smaller mesh
            // entities. The generator extends the native point set with the
            // generated midpoints/centre, so every child remains a subset.
            vtkIdType seedPoint = nativePointIds.front();
            int smallestCandidateCount =
                entityIndicesByPoint[boundaryDimension].value(seedPoint).size();
            for (vtkIdType pointId : nativePointIds)
            {
                const int candidateCount =
                    entityIndicesByPoint[boundaryDimension].value(pointId).size();
                if (candidateCount < smallestCandidateCount)
                {
                    seedPoint = pointId;
                    smallestCandidateCount = candidateCount;
                }
            }

            QSet<vtkIdType> nativePointSet;
            for (vtkIdType pointId : nativePointIds)
                nativePointSet.insert(pointId);
            for (int entityIndex :
                 entityIndicesByPoint[boundaryDimension].value(seedPoint))
            {
                if (entityIndex < 0 ||
                    entityIndex >= gridEntities[boundaryDimension].size())
                    continue;
                const FacetRef& candidate =
                    gridEntities[boundaryDimension].at(entityIndex);
                bool isSubset = true;
                for (vtkIdType pointId : candidate.pointIds)
                    isSubset = isSubset && nativePointSet.contains(pointId);
                if (isSubset)
                    matchingEntities.push_back(entityIndex);
            }
        }

        for (int entityIndex : matchingEntities)
        {
            if (entityIndex < 0 ||
                entityIndex >= gridEntities[boundaryDimension].size())
                continue;
            const FacetRef& ref = gridEntities[boundaryDimension].at(entityIndex);
            const MeshIdType entityKey =
                (static_cast<MeshIdType>(ref.cellId) << 4) |
                (static_cast<MeshIdType>(ref.localId) & 0xF);
            if (boundaryDimension == 2)
                set.faceKeys.push_back(entityKey);
            else
                set.edgeKeys.push_back(entityKey);
        }
    }

    QVector<MeshTagKey> emptyKeys;
    for (auto it = pick.tagSets.begin(); it != pick.tagSets.end(); ++it)
    {
        MeshTagSet& set = it.value();
        if (set.key.category.compare(QStringLiteral("SmeshBoundary"),
                                     Qt::CaseInsensitive) != 0)
            continue;
        normalizeMeshTagSet(set);
        if (set.pointIds.isEmpty())
        {
            emptyKeys.push_back(set.key);
            continue;
        }
        for (MeshIdType pointId : set.pointIds)
        {
            QVector<MeshTagKey>& tags = pick.pointIdToBoundaryTags[pointId];
            if (!tags.contains(set.key))
                tags.push_back(set.key);
        }
    }
    for (const MeshTagKey& key : emptyKeys)
        pick.tagSets.remove(key);
}

static void syncPickMapGroupingFromGrid(vtkUnstructuredGrid* grid)
{
    if (!grid)
        return;

    MshPickMap pick = MeshGridIO::instance().getMeshMap();
    auto* regionCellData = grid->GetCellData()
        ? vtkIntArray::SafeDownCast(grid->GetCellData()->GetArray("src_part"))
        : nullptr;
    bool hasUsableRegionCellData = false;
    for (vtkIdType cid = 0; regionCellData && cid < regionCellData->GetNumberOfTuples(); ++cid)
    {
        if (regionCellData->GetValue(cid) > 0)
        {
            hasUsableRegionCellData = true;
            break;
        }
    }

    // IIIN snapshots omit src_part/GroupName. Recover the authoritative source
    // regions from mesh_map.json before rebuilding tags or the display panel.
    if (!hasUsableRegionCellData && grid->GetCellData())
    {
        auto restoredRegions = vtkSmartPointer<vtkIntArray>::New();
        restoredRegions->SetName("src_part");
        restoredRegions->SetNumberOfTuples(grid->GetNumberOfCells());
        restoredRegions->FillValue(0);
        auto restoredNames = vtkSmartPointer<vtkStringArray>::New();
        restoredNames->SetName("GroupName");
        restoredNames->SetNumberOfValues(grid->GetNumberOfCells());
        for (auto it = pick.tagSets.constBegin(); it != pick.tagSets.constEnd(); ++it)
        {
            const MeshTagSet& set = it.value();
            if (set.key.category.compare("REGION", Qt::CaseInsensitive) != 0 || set.key.id <= 0)
                continue;
            for (MeshIdType cid : set.cellIds)
            {
                if (cid < 0 || cid >= grid->GetNumberOfCells() ||
                    meshDisplayCellDimensionFast(grid, cid) != set.key.dim)
                    continue;
                restoredRegions->SetValue(cid, set.key.id);
                restoredNames->SetValue(cid, set.name.toStdString());
                hasUsableRegionCellData = true;
            }
        }
        if (hasUsableRegionCellData)
        {
            grid->GetCellData()->AddArray(restoredRegions);
            grid->GetCellData()->AddArray(restoredNames);
            regionCellData = restoredRegions;
        }
    }

    int meshDim = -1;
    const vtkIdType nCells = grid->GetNumberOfCells();

    for (vtkIdType cid = 0; cid < nCells; ++cid)
    {
        vtkCell* cell = grid->GetCell(cid);

        if (!cell)
            continue;

        meshDim = qMax(meshDim, meshDisplayCellDimensionFast(grid, cid));
    }

    if (meshDim < 0)
        return;

           // meshDim 表示模型最高维度；MATYPE/LAYER 的 DomainKey.dim 不能全部使用 meshDim。
           // 混合网格中杆单元必须生成 dim=1，否则 MaterialManager/PickManager 无法把 lineGrid
           // 当作独立域进行高亮。
    pick.meshDim = meshDim;

    // The point table belongs to the current grid. Rebuild both directions
    // every time instead of retaining ids from a previously imported mesh.
    // SMESH writes node_id directly into the generated VTK, so no auxiliary
    // mesh-map file is needed for node picking and solver export.
    pick.nodeIdToPointId.clear();
    pick.pointIdToNodeId.clear();
    auto* nodeIdArr = grid->GetPointData()
        ? grid->GetPointData()->GetArray("node_id")
        : nullptr;
    for (vtkIdType pid = 0; pid < grid->GetNumberOfPoints(); ++pid)
    {
        const int nodeId = nodeIdArr && pid < nodeIdArr->GetNumberOfTuples()
            ? static_cast<int>(nodeIdArr->GetTuple1(pid))
            : static_cast<int>(pid + 1);
        if (nodeId <= 0)
            continue;
        pick.nodeIdToPointId.insert(nodeId, static_cast<int>(pid));
        pick.pointIdToNodeId.insert(static_cast<int>(pid), nodeId);
    }

    // 所有网格入口（INP、FLAC3D、VTK、工程恢复及重新分组）都在这里
    // 统一同步实际网格维度，避免求解模块读取到未初始化或过期的全局值。
    // A line-only model still has 2D/3D spatial coordinates. Publishing 1 here
    // makes load exporters fall into their 3D branch while IIIN remains 2D.
    int spatialDim=meshDim;
    if(meshDim==1)
    {
        double bounds[6];grid->GetBounds(bounds);
        spatialDim=std::abs(bounds[5]-bounds[4])>1.e-12?3:2;
    }
    PublicVariables::instance().set(PublicConstants::kMeshDimension, spatialDim, "mesh");

    QVector<MeshTagKey> removeKeys;

    for (auto it = pick.tagSets.constBegin(); it != pick.tagSets.constEnd(); ++it)
    {
        const MeshTagKey& key = it.key();

        const bool isMatype =
            key.category.compare("MATYPE", Qt::CaseInsensitive) == 0;

        const bool isLayer =
            key.category.compare("LAYER", Qt::CaseInsensitive) == 0;

        const bool isRegion =
            key.category.compare("REGION", Qt::CaseInsensitive) == 0;

        // Generated SMESH VTK files carry src_part and are authoritative.
        // IIIN project snapshots do not carry that extra array; in that case
        // retain REGION sets restored from mesh_map.json instead of deleting
        // the excavation grouping during project load.
        if (isMatype || isLayer || (isRegion && hasUsableRegionCellData))
            removeKeys.push_back(key);
    }

    for (const MeshTagKey& key : removeKeys)
        pick.tagSets.remove(key);

    pick.cellIdToDomainTags.clear();

    auto* matArr =
        vtkIntArray::SafeDownCast(grid->GetCellData()->GetArray("MATYPE"));

    auto* layArr =
        vtkIntArray::SafeDownCast(grid->GetCellData()->GetArray("LAYER"));
    auto* regionArr = regionCellData;
    auto* groupNameArr =
        vtkStringArray::SafeDownCast(grid->GetCellData()->GetAbstractArray("GroupName"));

    auto addDomainTag = [&](const QString& category, int dim, int id, vtkIdType cid)
    {
        if (dim <= 0)
            return;

        const bool isLayer =
            category.compare("LAYER", Qt::CaseInsensitive) == 0;

        if (isLayer)
        {
            if (id == 0)
                return;
        }
        else
        {
            if (id <= 0)
                return;
        }

        MeshTagKey key;
        key.category = category;
        key.dim = dim;
        key.id = id;

        if (!pick.tagSets.contains(key))
        {
            MeshTagSet set;
            set.key = key;
            // GroupName describes a SMESH source geometry region. It must
            // not become the label of the common MATYPE/LAYER domains: all
            // SMESH cells initially have LAYER=1, so that made the whole-model
            // layer look like one particular geometry part in combo boxes.
            const bool isRegion =
                category.compare("REGION", Qt::CaseInsensitive) == 0;
            const QString groupName = isRegion && groupNameArr &&
                    cid < groupNameArr->GetNumberOfValues()
                ? QString::fromStdString(groupNameArr->GetValue(cid)).trimmed()
                : QString();
            const QString labelKey = QStringLiteral("%1/%2/%3").arg(category).arg(dim).arg(id);
            const QString savedLabel = pick.groupDisplayNames.value(labelKey).trimmed();
            set.name = !savedLabel.isEmpty() ? savedLabel
                : (groupName.isEmpty() ? QString("%1=%2").arg(category).arg(id) : groupName);
            set.source = "GridCellData";

            pick.tagSets.insert(key, set);
        }

        pick.tagSets[key].cellIds.push_back(static_cast<MeshIdType>(cid));

        auto& tags = pick.cellIdToDomainTags[static_cast<MeshIdType>(cid)];

        if (!tags.contains(key))
            tags.push_back(key);
    };

    for (vtkIdType cid = 0; cid < nCells; ++cid)
    {
        vtkCell* cell = grid->GetCell(cid);

        if (!cell)
            continue;

        const int cellDim = meshDisplayCellDimensionFast(grid, cid);
        if (cellDim <= 0)
            continue;

        const int mat = matArr ? matArr->GetValue(cid) : 0;
        const int lay = layArr ? layArr->GetValue(cid) : 0;
        const int region = regionArr ? regionArr->GetValue(cid) : 0;

        addDomainTag("MATYPE", cellDim, mat, cid);
        addDomainTag("LAYER", cellDim, lay, cid);
        addDomainTag("REGION", cellDim, region, cid);
    }

           // 保留并重建已有 Physical 域标签的 cellId -> tags 反查。
           // MATYPE/LAYER 已按单元真实维度重建；Physical 仍按原导入标签保留。
    for (auto it = pick.tagSets.constBegin(); it != pick.tagSets.constEnd(); ++it)
    {
        const MeshTagSet& set = it.value();

        if (set.cellIds.isEmpty())
            continue;

        const bool isMatype =
            set.key.category.compare("MATYPE", Qt::CaseInsensitive) == 0;
        const bool isLayer =
            set.key.category.compare("LAYER", Qt::CaseInsensitive) == 0;

        if (isMatype || isLayer)
            continue;

        if (set.key.dim != pick.meshDim)
            continue;

        for (MeshIdType cid : set.cellIds)
        {
            auto& tags = pick.cellIdToDomainTags[cid];
            if (!tags.contains(set.key))
                tags.push_back(set.key);
        }
    }

    // Restore SMESH's native point/edge/face boundary regions carried by VTK.
    // They intentionally remain distinct from imported Physical groups internally.
    rebuildSmeshBoundaryTagSets(pick, grid, meshDim);

    MeshGridIO::instance().setMeshMap(pick);
}

vtkSmartPointer<vtkUnstructuredGrid> MeshModuleController::getVtkUnstructuredGridSolid()
{
    return m_previewSurfaceGrid;
}

vtkSmartPointer<vtkUnstructuredGrid> MeshModuleController::getVtkUnstructuredGridLine()
{
    return m_previewLineGrid;
}

bool MeshModuleController::autoGroupingBySpatialBins(int binCount, const QString& targetArrayName,
                                                     int startValue, const QString& groupPrefix,
                                                     QString* errMsg)
{
    if (!m_importedGrid || m_importedGrid->GetNumberOfCells() <= 0)
    {
        if (errMsg)
            *errMsg = QString("当前没有可分组的网格");
        return false;
    }
    if (binCount < 2)
    {
        if (errMsg)
            *errMsg = QString("自动分组数至少为 2");
        return false;
    }
    const bool targetIsMatype = targetArrayName.compare("MATYPE", Qt::CaseInsensitive) == 0;
    if ((targetIsMatype && startValue < FIRST_AUTO_MATYPE) ||
        (!targetIsMatype && startValue <= 0))
    {
        if (errMsg && targetIsMatype)
            *errMsg = QStringLiteral(
                "自动材料分组必须从编号 3 或更大编号开始；编号 1 不参加计算，编号 2 固定按线弹性处理");
        else if (errMsg)
            *errMsg = QString("起始编号必须大于 0");
        return false;
    }
    if (targetIsMatype && startValue + binCount - 1 > MAX_SOLVER_MATYPE)
    {
        if (errMsg)
        {
            *errMsg = QStringLiteral("自动分组后的材料编号不能超过 300，请减小起始编号或分组数量");
        }
        return false;
    }
    if (targetArrayName.trimmed().isEmpty())
    {
        if (errMsg)
            *errMsg = QString("目标分组字段不能为空");
        return false;
    }

    const vtkIdType nCells = m_importedGrid->GetNumberOfCells();
    struct CellCenter
    {
        vtkIdType cid;
        double x;
        double y;
        double z;
    };
    QVector<CellCenter> centers;
    centers.reserve((int)nCells);

    double minV[3] = {std::numeric_limits<double>::max(), std::numeric_limits<double>::max(),
                      std::numeric_limits<double>::max()};
    double maxV[3] = {-std::numeric_limits<double>::max(), -std::numeric_limits<double>::max(),
                      -std::numeric_limits<double>::max()};

    int highestDimension = 0;
    for (vtkIdType cid = 0; cid < nCells; ++cid)
        if (vtkCell* c = m_importedGrid->GetCell(cid))
            highestDimension = qMax(highestDimension, c->GetCellDimension());
    for (vtkIdType cid = 0; cid < nCells; ++cid)
    {
        auto* cell = m_importedGrid->GetCell(cid);
        if (!cell || highestDimension < 2 || cell->GetCellDimension() != highestDimension)
            continue;
        double b[6] = {0, 0, 0, 0, 0, 0};
        cell->GetBounds(b);
        const double cx = CENTER_FACTOR * (b[0] + b[1]);
        const double cy = CENTER_FACTOR * (b[2] + b[3]);
        const double cz = CENTER_FACTOR * (b[4] + b[5]);
        minV[0] = std::min(minV[0], cx);
        minV[1] = std::min(minV[1], cy);
        minV[2] = std::min(minV[2], cz);
        maxV[0] = std::max(maxV[0], cx);
        maxV[1] = std::max(maxV[1], cy);
        maxV[2] = std::max(maxV[2], cz);
        centers.push_back({cid, cx, cy, cz});
    }

    if (centers.isEmpty())
    {
        if (errMsg)
            *errMsg = QString("无法读取单元中心，自动分组失败");
        return false;
    }

    const double span[3] = {maxV[0] - minV[0], maxV[1] - minV[1], maxV[2] - minV[2]};
    int axis = 0;
    if (span[1] > span[axis])
        axis = 1;
    if (span[2] > span[axis])
        axis = 2;

           // 业务上「填筑层」更关注高程分层：优先按 Z 方向划分。
           // 若 Z 跨度近乎为 0（例如二维工况），再回退到最大跨度方向。
    if (targetArrayName.compare("LAYER", Qt::CaseInsensitive) == 0)
    {
        constexpr double kEps = 1e-12;
        if (span[2] > kEps)
            axis = 2;
    }

    auto* targetArr = vtkIntArray::SafeDownCast(
        m_importedGrid->GetCellData()->GetArray(targetArrayName.toUtf8().constData()));
    if (!targetArr)
    {
        auto newArr = vtkSmartPointer<vtkIntArray>::New();
        newArr->SetName(targetArrayName.toUtf8().constData());
        newArr->SetNumberOfComponents(1);
        newArr->SetNumberOfTuples(nCells);
        newArr->FillComponent(0, 0);
        m_importedGrid->GetCellData()->AddArray(newArr);
        targetArr = newArr;
    }

           // 使用“按主轴排序后的等数量分箱”（quantile
           // binning），避免仅按跨度均分导致某些组过密/过疏。
    std::sort(centers.begin(), centers.end(),
              [axis](const CellCenter& a, const CellCenter& b)
              {
                  const double av = (axis == 0 ? a.x : (axis == 1 ? a.y : a.z));
                  const double bv = (axis == 0 ? b.x : (axis == 1 ? b.y : b.z));
                  if (av == bv)
                      return a.cid < b.cid;
                  return av < bv;
              });

    const int n = centers.size();
    for (int i = 0; i < n; ++i)
    {
        int bin = (i * binCount) / n;
        if (bin >= binCount)
            bin = binCount - 1;
        if (bin < 0)
            bin = 0;

        const int targetValue = startValue + bin;
        const vtkIdType cid = centers[i].cid;
        targetArr->SetValue(cid, targetValue);
    }

    m_importedGrid->GetCellData()->Modified();
    m_importedGrid->Modified();
    {
        MshPickMap pick = MeshGridIO::instance().getMeshMap();
        int dim = 0;
        for (vtkIdType cid = 0; cid < nCells; ++cid)
            if (vtkCell* c = m_importedGrid->GetCell(cid)) dim = qMax(dim, c->GetCellDimension());
        for (int i = 0; i < binCount; ++i)
        {
            const int value = startValue + i;
            pick.groupDisplayNames[QStringLiteral("%1/%2/%3")
                .arg(targetArrayName.toUpper()).arg(dim).arg(value)] =
                QStringLiteral("%1_%2").arg(groupPrefix).arg(value);
        }
        MeshGridIO::instance().setMeshMap(pick);
    }
    syncPickMapGroupingFromGrid(m_importedGrid);
    displayCurrentGrid();
    return true;
}

bool MeshModuleController::groupByCoordinateRange(double xmin, double xmax, double ymin,
                                                  double ymax, double zmin, double zmax,
                                                  const QString& targetArrayName, int value,
                                                  const QString& groupName, QString* errMsg)
{
    if (!m_importedGrid || m_importedGrid->GetNumberOfCells() <= 0)
    {
        if (errMsg)
            *errMsg = QString("当前没有可分组的网格");
        return false;
    }
    const bool targetIsLayer = targetArrayName.compare("LAYER", Qt::CaseInsensitive) == 0;
    const bool targetIsMatype = targetArrayName.compare("MATYPE", Qt::CaseInsensitive) == 0;
    if ((targetIsLayer && value == 0) ||
        (targetIsMatype && value < FIRST_AUTO_MATYPE) ||
        (!targetIsLayer && !targetIsMatype && value <= 0))
    {
        if (errMsg && targetIsMatype)
            *errMsg = QStringLiteral(
                "新建材料分组编号必须大于等于 3；编号 1 不参加计算，编号 2 仅兼容旧数据");
        else if (errMsg)
            *errMsg = targetIsLayer ? QString("LAYER 分组编号不能为 0")
                                    : QString("分组编号必须大于 0");
        return false;
    }
    if (targetIsMatype && value > MAX_SOLVER_MATYPE)
    {
        if (errMsg)
            *errMsg = QStringLiteral("材料分组编号不能超过 300");
        return false;
    }
    if (targetArrayName.trimmed().isEmpty())
    {
        if (errMsg)
            *errMsg = QString("目标分组字段不能为空");
        return false;
    }

    if (xmin > xmax)
        std::swap(xmin, xmax);
    if (ymin > ymax)
        std::swap(ymin, ymax);
    if (zmin > zmax)
        std::swap(zmin, zmax);

    const vtkIdType nCells = m_importedGrid->GetNumberOfCells();
    auto* targetArr = vtkIntArray::SafeDownCast(
        m_importedGrid->GetCellData()->GetArray(targetArrayName.toStdString().c_str()));
    if (!targetArr)
    {
        auto newArr = vtkSmartPointer<vtkIntArray>::New();
        newArr->SetName(targetArrayName.toStdString().c_str());
        newArr->SetNumberOfComponents(1);
        newArr->SetNumberOfTuples(nCells);
        newArr->FillComponent(0, 0);
        m_importedGrid->GetCellData()->AddArray(newArr);
        targetArr = newArr;
    }

    int highestDimension = 0;
    for (vtkIdType cid = 0; cid < nCells; ++cid)
        if (vtkCell* c = m_importedGrid->GetCell(cid))
            highestDimension = qMax(highestDimension, c->GetCellDimension());
    int changed = 0;
    for (vtkIdType cid = 0; cid < nCells; ++cid)
    {
        auto* cell = m_importedGrid->GetCell(cid);
        if (!cell || highestDimension < 2 || cell->GetCellDimension() != highestDimension)
            continue;
        double b[6] = {0, 0, 0, 0, 0, 0};
        cell->GetBounds(b);
        const double cx = CENTER_FACTOR * (b[0] + b[1]);
        const double cy = CENTER_FACTOR * (b[2] + b[3]);
        const double cz = CENTER_FACTOR * (b[4] + b[5]);
        if (cx >= xmin && cx <= xmax && cy >= ymin && cy <= ymax && cz >= zmin && cz <= zmax)
        {
            targetArr->SetValue(cid, value);
            ++changed;
        }
    }

    if (changed == 0)
    {
        if (errMsg)
            *errMsg = QString("范围内没有命中任何单元");
        return false;
    }

    m_importedGrid->GetCellData()->Modified();
    m_importedGrid->Modified();
    {
        MshPickMap pick = MeshGridIO::instance().getMeshMap();
        int dim = 0;
        for (vtkIdType cid = 0; cid < nCells; ++cid)
            if (vtkCell* c = m_importedGrid->GetCell(cid))
                dim = qMax(dim, c->GetCellDimension());
        pick.groupDisplayNames[QStringLiteral("%1/%2/%3")
            .arg(targetArrayName.toUpper()).arg(dim).arg(value)] = groupName.trimmed();
        MeshGridIO::instance().setMeshMap(pick);
    }
    syncPickMapGroupingFromGrid(m_importedGrid);
    displayCurrentGrid();
    return true;
}

bool MeshModuleController::assignSelectedCellsToGroup(
    const QVector<MeshIdType>& cellIds, const QString& attribute, int groupId,
    const QString& displayName, QString* errMsg)
{
    const auto fail = [&](const QString& message) {
        if (errMsg) *errMsg = message;
        return false;
    };
    if (!m_importedGrid || m_importedGrid->GetNumberOfCells() == 0)
        return fail(QStringLiteral("请先导入或生成网格。"));
    const QString field = attribute.trimmed().toUpper();
    if (field != QStringLiteral("MATYPE") && field != QStringLiteral("LAYER"))
        return fail(QStringLiteral("仅允许修改 MATYPE 或 LAYER。"));
    if (field == QStringLiteral("MATYPE") && (groupId < 3 || groupId > MAX_SOLVER_MATYPE))
        return fail(QStringLiteral("新材料编号只能使用 3 至 300；1/2 为求解器保留编号。"));
    if (field == QStringLiteral("LAYER") && (groupId < 1 || groupId > 1000000))
        return fail(QStringLiteral("施工层号必须是 1 至 1000000；杆单元的负安装级次不可在此修改。"));
    if (cellIds.isEmpty())
        return fail(QStringLiteral("请先点选网格单元，或选择已有分组。"));

    const vtkIdType nCells = m_importedGrid->GetNumberOfCells();
    QSet<vtkIdType> uniqueIds;
    int meshDim = 0;
    for (vtkIdType cid = 0; cid < nCells; ++cid)
        if (vtkCell* c = m_importedGrid->GetCell(cid))
            meshDim = qMax(meshDim, c->GetCellDimension());
    // Preflight the ENTIRE edit: no partial writes, and never edit rod cells.
    for (MeshIdType raw : cellIds)
    {
        if (raw < 0 || raw >= nCells)
            return fail(QStringLiteral("选区包含过期单元编号，请重新选择。"));
        vtkCell* cell = m_importedGrid->GetCell(static_cast<vtkIdType>(raw));
        if (!cell || cell->GetCellDimension() != meshDim || meshDim < 2)
            return fail(QStringLiteral("选区包含杆单元或非最高维单元，请改选土体面/体单元。"));
        uniqueIds.insert(static_cast<vtkIdType>(raw));
    }
    auto* cd = m_importedGrid->GetCellData();
    vtkDataArray* existing = cd ? cd->GetArray(field.toUtf8().constData()) : nullptr;
    if (!cd)
        return fail(QStringLiteral("网格缺少单元属性表。"));
    auto* target = vtkIntArray::SafeDownCast(existing);
    if (!target || target->GetNumberOfTuples() != nCells)
    {
        vtkNew<vtkIntArray> converted;
        converted->SetName(field.toUtf8().constData());
        converted->SetNumberOfTuples(nCells);
        for (vtkIdType cid = 0; cid < nCells; ++cid)
            converted->SetValue(cid, existing && cid < existing->GetNumberOfTuples()
                                         ? static_cast<int>(existing->GetTuple1(cid)) : 0);
        cd->RemoveArray(field.toUtf8().constData());
        cd->AddArray(converted);
        target = vtkIntArray::SafeDownCast(cd->GetArray(field.toUtf8().constData()));
    }
    if (!target)
        return fail(QStringLiteral("无法创建目标单元属性。"));
    for (vtkIdType cid : uniqueIds)
        target->SetValue(cid, groupId);
    target->Modified();
    cd->Modified();
    m_importedGrid->Modified();
    // Only this attribute changes: GroupName is not a solver grouping field.
    MshPickMap pick = MeshGridIO::instance().getMeshMap();
    const QString key = QStringLiteral("%1/%2/%3").arg(field).arg(meshDim).arg(groupId);
    if (!displayName.trimmed().isEmpty())
        pick.groupDisplayNames.insert(key, displayName.trimmed());
    MeshGridIO::instance().setMeshMap(pick);
    syncPickMapGroupingFromGrid(m_importedGrid);
    displayCurrentGrid();
    return true;
}

bool MeshModuleController::assignDamConstructionLayers(
    const QSet<MeshIdType>& foundationCellIds,
    const QSet<MeshIdType>& damCellIds,
    int fillLayerCount,
    QString* errMsg)
{
    const auto fail = [errMsg](const QString& message)
    {
        if (errMsg)
            *errMsg = message;
        return false;
    };

    if (!m_importedGrid || m_importedGrid->GetNumberOfCells() <= 0)
        return fail(QStringLiteral("当前没有可设置施工分层的网格。"));
    if (foundationCellIds.isEmpty())
        return fail(QStringLiteral("请至少指定一个坝基区域。"));
    if (damCellIds.isEmpty())
        return fail(QStringLiteral("请至少指定一个坝体区域。"));
    if (fillLayerCount <= 0)
        return fail(QStringLiteral("填筑层数必须为正整数。"));

    for (MeshIdType cid : foundationCellIds)
    {
        if (damCellIds.contains(cid))
            return fail(QStringLiteral("坝基区域与坝体区域存在重叠，请重新选择。"));
    }

    int targetDim = PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
    if (targetDim < 2 || targetDim > 3)
    {
        targetDim = 0;
        for (vtkIdType cid = 0; cid < m_importedGrid->GetNumberOfCells(); ++cid)
        {
            if (vtkCell* cell = m_importedGrid->GetCell(cid))
                targetDim = qMax(targetDim, cell->GetCellDimension());
        }
    }

    const vtkIdType cellCount = m_importedGrid->GetNumberOfCells();
    vtkDataArray* materialArray = m_importedGrid->GetCellData()->GetArray("MATYPE");
    QSet<vtkIdType> foundation;
    QSet<vtkIdType> dam;
    auto collectValidCells = [&](const QSet<MeshIdType>& source, QSet<vtkIdType>* target) -> bool
    {
        for (MeshIdType rawId : source)
        {
            const vtkIdType cid = static_cast<vtkIdType>(rawId);
            if (cid < 0 || cid >= cellCount)
                return false;
            vtkCell* cell = m_importedGrid->GetCell(cid);
            if (cell && cell->GetCellDimension() == targetDim)
                target->insert(cid);
        }
        return true;
    };
    if (!collectValidCells(foundationCellIds, &foundation) ||
        !collectValidCells(damCellIds, &dam))
    {
        return fail(QStringLiteral("所选区域包含无效网格单元，请刷新网格后重试。"));
    }
    if (foundation.isEmpty() || dam.isEmpty())
        return fail(QStringLiteral("所选区域中没有当前维度的实体单元。"));

    int unassignedCount = 0;
    for (vtkIdType cid = 0; cid < cellCount; ++cid)
    {
        vtkCell* cell = m_importedGrid->GetCell(cid);
        if (!cell || cell->GetCellDimension() != targetDim)
            continue;
        const int material = materialArray ? int(materialArray->GetTuple1(cid)) : 2;
        if (material == 1)
            continue;
        if (!foundation.contains(cid) && !dam.contains(cid))
            ++unassignedCount;
    }
    if (unassignedCount > 0)
    {
        return fail(QStringLiteral("还有 %1 个实体单元未归入坝基或坝体区域。")
                        .arg(unassignedCount));
    }

    QSet<int> foundationMaterials;
    QSet<int> damMaterials;
    if (materialArray)
    {
        for (vtkIdType cid : std::as_const(foundation))
        {
            const int material = int(materialArray->GetTuple1(cid));
            if (material != 1)
                foundationMaterials.insert(material);
        }
        for (vtkIdType cid : std::as_const(dam))
        {
            const int material = int(materialArray->GetTuple1(cid));
            if (material != 1)
                damMaterials.insert(material);
        }
        QStringList conflicts;
        for (int material : std::as_const(foundationMaterials))
        {
            if (damMaterials.contains(material))
                conflicts << QString::number(material);
        }
        if (!conflicts.isEmpty())
        {
            return fail(QStringLiteral("坝基与坝体不能共用材料号 %1，请先分别设置材料。")
                            .arg(conflicts.join(QStringLiteral("、"))));
        }
    }

    const int axis = qBound(1, m_verticalAxis, 3) - 1;
    struct DamCellCenter
    {
        vtkIdType cellId = -1;
        double elevation = 0.0;
    };
    QVector<DamCellCenter> damCenters;
    damCenters.reserve(dam.size());
    double minimumElevation = std::numeric_limits<double>::max();
    double maximumElevation = -std::numeric_limits<double>::max();
    for (vtkIdType cid : std::as_const(dam))
    {
        double bounds[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
        m_importedGrid->GetCell(cid)->GetBounds(bounds);
        const double elevation = CENTER_FACTOR * (bounds[axis * 2] + bounds[axis * 2 + 1]);
        damCenters.push_back({cid, elevation});
        minimumElevation = qMin(minimumElevation, elevation);
        maximumElevation = qMax(maximumElevation, elevation);
    }
    if (fillLayerCount > 1 && maximumElevation <= minimumElevation)
        return fail(QStringLiteral("坝体单元在竖向上没有足够跨度，无法划分多个填筑层。"));

    QHash<vtkIdType, int> plannedLayers;
    QVector<int> layerCellCounts(fillLayerCount, 0);
    const double span = maximumElevation - minimumElevation;
    for (const DamCellCenter& item : std::as_const(damCenters))
    {
        int bin = 0;
        if (fillLayerCount > 1 && span > 0.0)
        {
            const double normalized = (item.elevation - minimumElevation) / span;
            bin = qMin(fillLayerCount - 1, int(std::floor(normalized * fillLayerCount)));
        }
        plannedLayers.insert(item.cellId, 2 + bin);
        ++layerCellCounts[bin];
    }
    for (int i = 0; i < layerCellCounts.size(); ++i)
    {
        if (layerCellCounts.at(i) == 0)
        {
            return fail(QStringLiteral("填筑层数过多，第 %1 个填筑层没有网格单元。")
                            .arg(i + 1));
        }
    }

    auto* layerArray = vtkIntArray::SafeDownCast(
        m_importedGrid->GetCellData()->GetArray("LAYER"));
    if (!layerArray)
    {
        auto newLayerArray = vtkSmartPointer<vtkIntArray>::New();
        newLayerArray->SetName("LAYER");
        newLayerArray->SetNumberOfComponents(1);
        newLayerArray->SetNumberOfTuples(cellCount);
        newLayerArray->FillComponent(0, 1);
        m_importedGrid->GetCellData()->AddArray(newLayerArray);
        layerArray = newLayerArray;
    }

    for (vtkIdType cid : std::as_const(foundation))
        layerArray->SetValue(cid, 1);
    for (auto it = plannedLayers.constBegin(); it != plannedLayers.constEnd(); ++it)
        layerArray->SetValue(it.key(), it.value());

    layerArray->Modified();
    m_importedGrid->GetCellData()->Modified();
    m_importedGrid->Modified();
    syncPickMapGroupingFromGrid(m_importedGrid);
    displayCurrentGrid();
    return true;
}

bool MeshModuleController::appendBarElements(const BarElementPlacement& placement,
                                             QString* errMsg)
{
    if (errMsg)
        errMsg->clear();

    const auto fail = [errMsg](const QString& message)
    {
        if (errMsg)
            *errMsg = message;
        return false;
    };
    if (!m_importedGrid || m_importedGrid->GetNumberOfCells() <= 0 ||
        !m_importedGrid->GetPoints())
        return fail(QStringLiteral("请先导入或生成实体网格。"));
    normalizeGridAttributeTupleCounts(m_importedGrid);
    if (placement.segmentCount <= 0 || placement.arrayCount <= 0)
        return fail(QStringLiteral("分段数和阵列数量必须为正整数。"));
    if (placement.arrayCount > 1)
    {
        const double offsetLength = std::sqrt(
            placement.arrayOffset[0] * placement.arrayOffset[0]
            + placement.arrayOffset[1] * placement.arrayOffset[1]
            + placement.arrayOffset[2] * placement.arrayOffset[2]);
        if (offsetLength <= 1.0e-12)
            return fail(QStringLiteral("阵列数量大于 1 时，阵列偏移不能为零。"));
    }

    const double dx = placement.end[0] - placement.start[0];
    const double dy = placement.end[1] - placement.start[1];
    const double dz = placement.end[2] - placement.start[2];
    const double rodLength = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (rodLength <= 1.0e-12)
        return fail(QStringLiteral("杆单元起点和终点不能重合。"));

    const vtkIdType oldPointCount = m_importedGrid->GetNumberOfPoints();
    const vtkIdType oldCellCount = m_importedGrid->GetNumberOfCells();
    const vtkIdType addedCellCount =
        static_cast<vtkIdType>(placement.arrayCount) * placement.segmentCount;

    // 一个“锚杆布置”工程节点统一使用一个 MATYPE。界面已指定时直接使用；
    // 旧工程或未指定时仍保留自动分配，以兼容历史数据。
    int rodMatype = placement.matype;
    if (rodMatype != 0 &&
        (rodMatype < FIRST_AUTO_MATYPE || rodMatype > MAX_SOLVER_MATYPE))
    {
        return fail(QStringLiteral("锚杆材料编号必须在 3 至 300 之间。"));
    }

    if (rodMatype == 0)
    {
        QSet<int> usedMatypes;
        if (vtkDataArray* existingMatypes =
                m_importedGrid->GetCellData()->GetArray("MATYPE"))
        {
            for (vtkIdType cid = 0;
                 cid < oldCellCount && cid < existingMatypes->GetNumberOfTuples(); ++cid)
            {
                const int value = static_cast<int>(existingMatypes->GetTuple1(cid));
                if (value >= FIRST_AUTO_MATYPE && value <= MAX_SOLVER_MATYPE)
                    usedMatypes.insert(value);
            }
        }
        if (auto* materialService =
                ServiceRegistry::instance().getService<MaterialManagerService>())
        {
            const QMap<int, QString> configured = materialService->calculationMaterialLabels();
            for (auto it = configured.constBegin(); it != configured.constEnd(); ++it)
            {
                if (it.key() >= FIRST_AUTO_MATYPE && it.key() <= MAX_SOLVER_MATYPE)
                    usedMatypes.insert(it.key());
            }
        }
        for (int candidate = FIRST_AUTO_MATYPE; candidate <= MAX_SOLVER_MATYPE; ++candidate)
        {
            if (!usedMatypes.contains(candidate))
            {
                rodMatype = candidate;
                break;
            }
        }
        if (rodMatype < FIRST_AUTO_MATYPE)
            return fail(QStringLiteral("材料编号 3 至 300 已全部使用，无法为新增锚杆分配材料编号。"));
    }

    int recordId = 1;
    if (vtkDataArray* records = m_importedGrid->GetCellData()->GetArray("BAR_GROUP"))
    {
        for (vtkIdType cid = 0; cid < oldCellCount; ++cid)
        {
            vtkCell* cell = m_importedGrid->GetCell(cid);
            if (cell && cell->GetCellDimension() == 1)
                recordId = qMax(recordId, static_cast<int>(records->GetTuple1(cid)) + 1);
        }
    }

    // Keep the original topology and all existing attributes intact. Only new
    // points and line cells are appended to this independent deep copy.
    auto result = vtkSmartPointer<vtkUnstructuredGrid>::New();
    result->DeepCopy(m_importedGrid);
    if (!result->GetPoints() || result->GetNumberOfPoints() != oldPointCount ||
        result->GetNumberOfCells() != oldCellCount)
    {
        return fail(QStringLiteral("复制原网格失败，无法生成杆单元。"));
    }
    vtkPoints* points = result->GetPoints();

    double bounds[6]{};
    m_importedGrid->GetBounds(bounds);
    const double diagonal = std::sqrt(
        (bounds[1] - bounds[0]) * (bounds[1] - bounds[0])
        + (bounds[3] - bounds[2]) * (bounds[3] - bounds[2])
        + (bounds[5] - bounds[4]) * (bounds[5] - bounds[4]));
    // IIIN.DAT currently writes five decimal places. A smaller merge tolerance can
    // produce different node ids that are exported to exactly the same coordinate.
    const double reuseTolerance = qMax(5.0e-6, qMin(1.0e-3, diagonal * 1.0e-10));
    const double reuseToleranceSquared = reuseTolerance * reuseTolerance;

    // Locator for every point already present in the grid, including previous bars.
    // It is used only to reuse coordinates that are already identical within the IIIN
    // output precision. It must not move an anchor point to a nearby mesh node.
    auto pointLocator = vtkSmartPointer<vtkPointLocator>::New();
    pointLocator->SetDataSet(m_importedGrid);
    pointLocator->BuildLocator();

    // 建模层保留用户给出的锚杆坐标，不在这里强制裁剪、吸附或拒绝实体外节点。
    // 节点与实体的耦合关系由后续求解输入生成阶段处理。
    int hostDimension = 0;
    for (vtkIdType cid = 0; cid < oldCellCount; ++cid)
    {
        vtkCell* cell = m_importedGrid->GetCell(cid);
        if (cell)
            hostDimension = qMax(hostDimension, cell->GetCellDimension());
    }
    if (hostDimension < 2)
        return fail(QStringLiteral("当前网格中没有可与锚杆耦合的实体单元。"));

    // 锚杆可以和其他“锚杆布置”节点共用同一个 MATYPE，但不能复用实体域
    // 已在使用的 MATYPE。否则同一材料编号会同时指向围岩和杆材料，导出 IMAT 时
    // 含义不再唯一。
    if (vtkDataArray* existingMatypes = m_importedGrid->GetCellData()->GetArray("MATYPE"))
    {
        for (vtkIdType cid = 0; cid < oldCellCount; ++cid)
        {
            vtkCell* cell = m_importedGrid->GetCell(cid);
            if (!cell || cell->GetCellDimension() != hostDimension ||
                cid >= existingMatypes->GetNumberOfTuples())
                continue;
            if (static_cast<int>(existingMatypes->GetTuple1(cid)) == rodMatype)
            {
                return fail(QStringLiteral(
                    "材料编号 %1 已被实体网格使用。锚杆材料编号可以在不同锚杆布置节点间复用，"
                    "但不要与实体材料编号共用。")
                                .arg(rodMatype));
            }
        }
    }

    const auto distanceSquared = [](const double a[3], const double b[3])
    {
        return (a[0] - b[0]) * (a[0] - b[0])
               + (a[1] - b[1]) * (a[1] - b[1])
               + (a[2] - b[2]) * (a[2] - b[2]);
    };
    const auto bucketCoordinate = [reuseTolerance](double value) -> qint64
    {
        return static_cast<qint64>(std::floor(value / reuseTolerance));
    };
    const auto bucketKey = [](qint64 x, qint64 y, qint64 z)
    {
        return QStringLiteral("%1:%2:%3").arg(x).arg(y).arg(z);
    };
    const auto pointKey = [reuseTolerance](const double xyz[3])
    {
        const qint64 x = qRound64(xyz[0] / reuseTolerance);
        const qint64 y = qRound64(xyz[1] / reuseTolerance);
        const qint64 z = qRound64(xyz[2] / reuseTolerance);
        return QStringLiteral("%1:%2:%3").arg(x).arg(y).arg(z);
    };
    const auto edgeKey = [&pointKey](const double first[3], const double second[3])
    {
        QString firstKey = pointKey(first);
        QString secondKey = pointKey(second);
        if (secondKey < firstKey)
            std::swap(firstKey, secondKey);
        return firstKey + QLatin1Char('|') + secondKey;
    };

    QSet<QString> occupiedEdges;
    for (vtkIdType cid = 0; cid < oldCellCount; ++cid)
    {
        vtkCell* cell = m_importedGrid->GetCell(cid);
        if (!cell || cell->GetCellType() != VTK_LINE || cell->GetNumberOfPoints() != 2)
            continue;
        double first[3]{};
        double second[3]{};
        m_importedGrid->GetPoint(cell->GetPointId(0), first);
        m_importedGrid->GetPoint(cell->GetPointId(1), second);
        occupiedEdges.insert(edgeKey(first, second));
    }

    // vtkPointLocator only knows the old grid. Keep a small spatial index for
    // points created during this operation so later bars can reuse them too.
    QHash<QString, QVector<vtkIdType>> newPointBuckets;
    const auto findCreatedPoint = [&](const double xyz[3]) -> vtkIdType
    {
        const qint64 bx = bucketCoordinate(xyz[0]);
        const qint64 by = bucketCoordinate(xyz[1]);
        const qint64 bz = bucketCoordinate(xyz[2]);
        for (qint64 ix = bx - 1; ix <= bx + 1; ++ix)
        {
            for (qint64 iy = by - 1; iy <= by + 1; ++iy)
            {
                for (qint64 iz = bz - 1; iz <= bz + 1; ++iz)
                {
                    const auto it = newPointBuckets.constFind(bucketKey(ix, iy, iz));
                    if (it == newPointBuckets.constEnd())
                        continue;
                    for (vtkIdType pid : it.value())
                    {
                        double existing[3]{};
                        points->GetPoint(pid, existing);
                        if (distanceSquared(existing, xyz) <= reuseToleranceSquared)
                            return pid;
                    }
                }
            }
        }
        return -1;
    };
    const auto registerCreatedPoint = [&](vtkIdType pid, const double xyz[3])
    {
        newPointBuckets[bucketKey(bucketCoordinate(xyz[0]), bucketCoordinate(xyz[1]),
                                  bucketCoordinate(xyz[2]))]
            .push_back(pid);
    };
    const auto findOldPoint = [&](const double xyz[3]) -> vtkIdType
    {
        const vtkIdType pid = pointLocator->FindClosestPoint(xyz);
        if (pid < 0)
            return -1;
        double existing[3]{};
        m_importedGrid->GetPoint(pid, existing);
        return distanceSquared(existing, xyz) <= reuseToleranceSquared ? pid : -1;
    };
    for (int copy = 0; copy < placement.arrayCount; ++copy)
    {
        QVector<vtkIdType> rodPointIds;
        rodPointIds.reserve(placement.segmentCount + 1);

        for (int segment = 0; segment <= placement.segmentCount; ++segment)
        {
            const double ratio = static_cast<double>(segment) / placement.segmentCount;
            double xyz[3]{};
            for (int axis = 0; axis < 3; ++axis)
            {
                xyz[axis] = placement.start[axis]
                            + ratio * (placement.end[axis] - placement.start[axis])
                            + copy * placement.arrayOffset[axis];
            }

            vtkIdType pointId = findOldPoint(xyz);
            if (pointId < 0)
                pointId = findCreatedPoint(xyz);

            if (pointId < 0)
            {
                pointId = points->InsertNextPoint(xyz);
                registerCreatedPoint(pointId, xyz);
            }
            rodPointIds.push_back(pointId);
        }

        for (int segment = 0; segment < placement.segmentCount; ++segment)
        {
            const vtkIdType firstId = rodPointIds.at(segment);
            const vtkIdType secondId = rodPointIds.at(segment + 1);
            if (firstId == secondId)
            {
                return fail(QStringLiteral("第 %1 根杆的第 %2 段长度过小，合并节点后退化为零长度单元。")
                                .arg(copy + 1)
                                .arg(segment + 1));
            }
            double first[3]{};
            double second[3]{};
            points->GetPoint(firstId, first);
            points->GetPoint(secondId, second);
            const QString currentEdgeKey = edgeKey(first, second);
            if (occupiedEdges.contains(currentEdgeKey))
            {
                return fail(QStringLiteral("第 %1 根杆的第 %2 段与已有杆单元重合，已取消本次生成。")
                                .arg(copy + 1)
                                .arg(segment + 1));
            }
            occupiedEdges.insert(currentEdgeKey);
            vtkIdType endpoints[2] = {firstId, secondId};
            result->InsertNextCell(VTK_LINE, 2, endpoints);
        }
    }

    const vtkIdType finalPointCount = points->GetNumberOfPoints();
    if (finalPointCount < oldPointCount ||
        result->GetNumberOfCells() != oldCellCount + addedCellCount)
    {
        return fail(QStringLiteral("追加杆单元后的网格数量校验失败，原网格未被修改。"));
    }

    normalizeGridAttributeTupleCounts(result, false, false);

    const auto ensureIntArray = [](vtkDataSetAttributes* attributes, const char* name,
                                   vtkIdType tupleCount, int defaultValue) -> vtkDataArray*
    {
        if (vtkDataArray* existing = attributes->GetArray(name))
            return existing;
        auto created = vtkSmartPointer<vtkIntArray>::New();
        created->SetName(name);
        created->SetNumberOfComponents(1);
        created->SetNumberOfTuples(tupleCount);
        created->FillComponent(0, defaultValue);
        attributes->AddArray(created);
        return created;
    };

    vtkDataArray* nodeIds = ensureIntArray(result->GetPointData(), "node_id",
                                           finalPointCount, 0);
    int maxNodeId = 0;
    for (vtkIdType pid = 0; pid < oldPointCount; ++pid)
    {
        int nodeId = static_cast<int>(nodeIds->GetTuple1(pid));
        if (nodeId <= 0)
        {
            nodeId = static_cast<int>(pid + 1);
            nodeIds->SetTuple1(pid, nodeId);
        }
        maxNodeId = qMax(maxNodeId, nodeId);
    }
    for (vtkIdType pid = oldPointCount; pid < finalPointCount; ++pid)
        nodeIds->SetTuple1(pid, ++maxNodeId);

    vtkDataArray* matypes = ensureIntArray(result->GetCellData(), "MATYPE",
                                           oldCellCount + addedCellCount, 2);
    vtkDataArray* layers = ensureIntArray(result->GetCellData(), "LAYER",
                                          oldCellCount + addedCellCount, 1);
    vtkDataArray* ketypes = ensureIntArray(result->GetCellData(), "KETYPE",
                                           oldCellCount + addedCellCount, 0);
    vtkDataArray* elementIds = ensureIntArray(result->GetCellData(), "elem_id",
                                              oldCellCount + addedCellCount, 0);
    ensureIntArray(result->GetCellData(), "src_part", oldCellCount + addedCellCount, 0);
    ensureIntArray(result->GetCellData(), "TAIL1", oldCellCount + addedCellCount, 0);
    ensureIntArray(result->GetCellData(), "TAIL2", oldCellCount + addedCellCount, 0);
    vtkDataArray* recordIds = ensureIntArray(result->GetCellData(), "BAR_GROUP",
                                             oldCellCount + addedCellCount, 0);

    auto* recordNames = vtkStringArray::SafeDownCast(
        result->GetCellData()->GetAbstractArray("BAR_GROUP_NAME"));
    if (!recordNames)
    {
        auto created = vtkSmartPointer<vtkStringArray>::New();
        created->SetName("BAR_GROUP_NAME");
        created->SetNumberOfValues(oldCellCount + addedCellCount);
        for (vtkIdType cid = 0; cid < oldCellCount + addedCellCount; ++cid)
            created->SetValue(cid, std::string());
        result->GetCellData()->AddArray(created);
        recordNames = created;
    }
    else if (recordNames->GetNumberOfValues() < oldCellCount + addedCellCount)
    {
        const vtkIdType oldValues = recordNames->GetNumberOfValues();
        recordNames->SetNumberOfValues(oldCellCount + addedCellCount);
        for (vtkIdType cid = oldValues; cid < oldCellCount + addedCellCount; ++cid)
            recordNames->SetValue(cid, std::string());
    }

    auto* placementUuids = vtkStringArray::SafeDownCast(
        result->GetCellData()->GetAbstractArray("BAR_PLACEMENT_UUID"));
    if (!placementUuids)
    {
        auto created = vtkSmartPointer<vtkStringArray>::New();
        created->SetName("BAR_PLACEMENT_UUID");
        created->SetNumberOfValues(oldCellCount + addedCellCount);
        for (vtkIdType cid = 0; cid < oldCellCount + addedCellCount; ++cid)
            created->SetValue(cid, std::string());
        result->GetCellData()->AddArray(created);
        placementUuids = created;
    }
    else if (placementUuids->GetNumberOfValues() < oldCellCount + addedCellCount)
    {
        const vtkIdType oldValues = placementUuids->GetNumberOfValues();
        placementUuids->SetNumberOfValues(oldCellCount + addedCellCount);
        for (vtkIdType cid = oldValues; cid < oldCellCount + addedCellCount; ++cid)
            placementUuids->SetValue(cid, std::string());
    }
    int maxElementId = 0;
    for (vtkIdType cid = 0; cid < oldCellCount; ++cid)
    {
        int elementId = static_cast<int>(elementIds->GetTuple1(cid));
        if (elementId <= 0)
        {
            elementId = static_cast<int>(cid + 1);
            elementIds->SetTuple1(cid, elementId);
        }
        maxElementId = qMax(maxElementId, elementId);
    }
    const int installationLayer = -qMax(1, qAbs(placement.installationLayer));
    const QString recordName = QStringLiteral("杆单元生成记录 %1").arg(recordId);
    for (vtkIdType cid = oldCellCount; cid < oldCellCount + addedCellCount; ++cid)
    {
        matypes->SetTuple1(cid, rodMatype);
        layers->SetTuple1(cid, installationLayer);
        ketypes->SetTuple1(cid, 1);
        elementIds->SetTuple1(cid, ++maxElementId);
        recordIds->SetTuple1(cid, recordId);
        recordNames->SetValue(cid, recordName.toStdString());
        placementUuids->SetValue(cid, placement.placementUuid.toStdString());
    }

    result->Modified();
    m_importedGrid = result;
    m_hasGrid = true;
    m_sourceFileName.clear();
    if (m_importSettingsWidget)
        m_importSettingsWidget->setSourceFileName(QString());
    m_splitCacheSource = nullptr;
    m_splitCacheLine = nullptr;
    m_splitCacheNonLine = nullptr;
    syncPickMapGroupingFromGrid(m_importedGrid);
    displayCurrentGrid();
    PickManager::instance().updateStructure();
    if (m_gridEditingWidget)
        m_gridEditingWidget->setEditableGrid(m_importedGrid, QString());
    return true;
}


bool MeshModuleController::appendBarElementsBatch(
    const QVector<BarElementPlacement>& placements, QString* errMsg)
{
    if (errMsg)
        errMsg->clear();

    const auto fail = [errMsg](const QString& message)
    {
        if (errMsg)
            *errMsg = message;
        return false;
    };
    if (placements.isEmpty())
        return fail(QStringLiteral("没有可生成的锚杆。"));
    if (!m_importedGrid || m_importedGrid->GetNumberOfCells() <= 0 ||
        !m_importedGrid->GetPoints())
        return fail(QStringLiteral("请先导入或生成实体网格。"));

    normalizeGridAttributeTupleCounts(m_importedGrid);

    const BarElementPlacement& firstPlacement = placements.first();
    for (int i = 0; i < placements.size(); ++i)
    {
        const BarElementPlacement& placement = placements.at(i);
        if (placement.segmentCount <= 0 || placement.arrayCount != 1)
            return fail(QStringLiteral("区域布置要求每根锚杆至少 1 段，且单条记录不再额外做阵列复制。"));
        if (placement.matype != firstPlacement.matype ||
            placement.installationLayer != firstPlacement.installationLayer ||
            placement.placementUuid != firstPlacement.placementUuid)
        {
            return fail(QStringLiteral("同一次区域布置中的锚杆必须属于同一布置节点和材料编号。"));
        }
        const double dx = placement.end[0] - placement.start[0];
        const double dy = placement.end[1] - placement.start[1];
        const double dz = placement.end[2] - placement.start[2];
        if (std::sqrt(dx * dx + dy * dy + dz * dz) <= 1.0e-12)
            return fail(QStringLiteral("第 %1 根锚杆的锚头与锚固端重合。").arg(i + 1));
    }

    const vtkIdType oldPointCount = m_importedGrid->GetNumberOfPoints();
    const vtkIdType oldCellCount = m_importedGrid->GetNumberOfCells();
    vtkIdType addedCellCount = 0;
    for (const BarElementPlacement& placement : placements)
        addedCellCount += placement.segmentCount;

    int rodMatype = firstPlacement.matype;
    if (rodMatype != 0 &&
        (rodMatype < FIRST_AUTO_MATYPE || rodMatype > MAX_SOLVER_MATYPE))
    {
        return fail(QStringLiteral("锚杆材料编号必须在 3 至 300 之间。"));
    }
    if (rodMatype == 0)
    {
        QSet<int> usedMatypes;
        if (vtkDataArray* existingMatypes = m_importedGrid->GetCellData()->GetArray("MATYPE"))
        {
            for (vtkIdType cid = 0;
                 cid < oldCellCount && cid < existingMatypes->GetNumberOfTuples(); ++cid)
            {
                const int value = static_cast<int>(existingMatypes->GetTuple1(cid));
                if (value >= FIRST_AUTO_MATYPE && value <= MAX_SOLVER_MATYPE)
                    usedMatypes.insert(value);
            }
        }
        if (auto* materialService =
                ServiceRegistry::instance().getService<MaterialManagerService>())
        {
            const QMap<int, QString> configured = materialService->calculationMaterialLabels();
            for (auto it = configured.constBegin(); it != configured.constEnd(); ++it)
            {
                if (it.key() >= FIRST_AUTO_MATYPE && it.key() <= MAX_SOLVER_MATYPE)
                    usedMatypes.insert(it.key());
            }
        }
        for (int candidate = FIRST_AUTO_MATYPE; candidate <= MAX_SOLVER_MATYPE; ++candidate)
        {
            if (!usedMatypes.contains(candidate))
            {
                rodMatype = candidate;
                break;
            }
        }
        if (rodMatype < FIRST_AUTO_MATYPE)
            return fail(QStringLiteral("材料编号 3 至 300 已全部使用，无法为新增锚杆分配材料编号。"));
    }

    int recordId = 1;
    if (vtkDataArray* records = m_importedGrid->GetCellData()->GetArray("BAR_GROUP"))
    {
        for (vtkIdType cid = 0; cid < oldCellCount; ++cid)
        {
            vtkCell* cell = m_importedGrid->GetCell(cid);
            if (cell && cell->GetCellDimension() == 1)
                recordId = qMax(recordId, static_cast<int>(records->GetTuple1(cid)) + 1);
        }
    }

    auto result = vtkSmartPointer<vtkUnstructuredGrid>::New();
    result->DeepCopy(m_importedGrid);
    if (!result->GetPoints() || result->GetNumberOfPoints() != oldPointCount ||
        result->GetNumberOfCells() != oldCellCount)
    {
        return fail(QStringLiteral("复制原网格失败，无法生成锚杆。"));
    }
    vtkPoints* points = result->GetPoints();

    double bounds[6]{};
    m_importedGrid->GetBounds(bounds);
    const double diagonal = std::sqrt(
        (bounds[1] - bounds[0]) * (bounds[1] - bounds[0])
        + (bounds[3] - bounds[2]) * (bounds[3] - bounds[2])
        + (bounds[5] - bounds[4]) * (bounds[5] - bounds[4]));
    const double reuseTolerance = qMax(5.0e-6, qMin(1.0e-3, diagonal * 1.0e-10));
    const double reuseToleranceSquared = reuseTolerance * reuseTolerance;

    auto pointLocator = vtkSmartPointer<vtkPointLocator>::New();
    pointLocator->SetDataSet(m_importedGrid);
    pointLocator->BuildLocator();

    int hostDimension = 0;
    for (vtkIdType cid = 0; cid < oldCellCount; ++cid)
    {
        if (vtkCell* cell = m_importedGrid->GetCell(cid))
            hostDimension = qMax(hostDimension, cell->GetCellDimension());
    }
    if (hostDimension < 2)
        return fail(QStringLiteral("当前网格中没有可与锚杆耦合的实体单元。"));

    if (vtkDataArray* existingMatypes = m_importedGrid->GetCellData()->GetArray("MATYPE"))
    {
        for (vtkIdType cid = 0; cid < oldCellCount; ++cid)
        {
            vtkCell* cell = m_importedGrid->GetCell(cid);
            if (!cell || cell->GetCellDimension() != hostDimension ||
                cid >= existingMatypes->GetNumberOfTuples())
                continue;
            if (static_cast<int>(existingMatypes->GetTuple1(cid)) == rodMatype)
            {
                return fail(QStringLiteral(
                    "材料编号 %1 已被实体网格使用，请为锚杆使用独立材料编号。")
                                .arg(rodMatype));
            }
        }
    }

    const auto distanceSquared = [](const double a[3], const double b[3])
    {
        return (a[0] - b[0]) * (a[0] - b[0])
               + (a[1] - b[1]) * (a[1] - b[1])
               + (a[2] - b[2]) * (a[2] - b[2]);
    };
    const auto bucketCoordinate = [reuseTolerance](double value) -> qint64
    {
        return static_cast<qint64>(std::floor(value / reuseTolerance));
    };
    const auto bucketKey = [](qint64 x, qint64 y, qint64 z)
    {
        return QStringLiteral("%1:%2:%3").arg(x).arg(y).arg(z);
    };
    const auto pointKey = [reuseTolerance](const double xyz[3])
    {
        const qint64 x = qRound64(xyz[0] / reuseTolerance);
        const qint64 y = qRound64(xyz[1] / reuseTolerance);
        const qint64 z = qRound64(xyz[2] / reuseTolerance);
        return QStringLiteral("%1:%2:%3").arg(x).arg(y).arg(z);
    };
    const auto edgeKey = [&pointKey](const double first[3], const double second[3])
    {
        QString firstKey = pointKey(first);
        QString secondKey = pointKey(second);
        if (secondKey < firstKey)
            std::swap(firstKey, secondKey);
        return firstKey + QLatin1Char('|') + secondKey;
    };

    QSet<QString> occupiedEdges;
    for (vtkIdType cid = 0; cid < oldCellCount; ++cid)
    {
        vtkCell* cell = m_importedGrid->GetCell(cid);
        if (!cell || cell->GetCellType() != VTK_LINE || cell->GetNumberOfPoints() != 2)
            continue;
        double first[3]{};
        double second[3]{};
        m_importedGrid->GetPoint(cell->GetPointId(0), first);
        m_importedGrid->GetPoint(cell->GetPointId(1), second);
        occupiedEdges.insert(edgeKey(first, second));
    }

    QHash<QString, QVector<vtkIdType>> newPointBuckets;
    const auto findCreatedPoint = [&](const double xyz[3]) -> vtkIdType
    {
        const qint64 bx = bucketCoordinate(xyz[0]);
        const qint64 by = bucketCoordinate(xyz[1]);
        const qint64 bz = bucketCoordinate(xyz[2]);
        for (qint64 ix = bx - 1; ix <= bx + 1; ++ix)
        {
            for (qint64 iy = by - 1; iy <= by + 1; ++iy)
            {
                for (qint64 iz = bz - 1; iz <= bz + 1; ++iz)
                {
                    const auto it = newPointBuckets.constFind(bucketKey(ix, iy, iz));
                    if (it == newPointBuckets.constEnd())
                        continue;
                    for (vtkIdType pid : it.value())
                    {
                        double existing[3]{};
                        points->GetPoint(pid, existing);
                        if (distanceSquared(existing, xyz) <= reuseToleranceSquared)
                            return pid;
                    }
                }
            }
        }
        return -1;
    };
    const auto registerCreatedPoint = [&](vtkIdType pid, const double xyz[3])
    {
        newPointBuckets[bucketKey(bucketCoordinate(xyz[0]), bucketCoordinate(xyz[1]),
                                  bucketCoordinate(xyz[2]))]
            .push_back(pid);
    };
    const auto findOldPoint = [&](const double xyz[3]) -> vtkIdType
    {
        const vtkIdType pid = pointLocator->FindClosestPoint(xyz);
        if (pid < 0)
            return -1;
        double existing[3]{};
        m_importedGrid->GetPoint(pid, existing);
        return distanceSquared(existing, xyz) <= reuseToleranceSquared ? pid : -1;
    };
    for (int barIndex = 0; barIndex < placements.size(); ++barIndex)
    {
        const BarElementPlacement& placement = placements.at(barIndex);
        QVector<vtkIdType> rodPointIds;
        rodPointIds.reserve(placement.segmentCount + 1);

        for (int nodeIndex = 0; nodeIndex <= placement.segmentCount; ++nodeIndex)
        {
            const double ratio = static_cast<double>(nodeIndex) / placement.segmentCount;
            const double xyz[3] = {
                placement.start[0] + (placement.end[0] - placement.start[0]) * ratio,
                placement.start[1] + (placement.end[1] - placement.start[1]) * ratio,
                placement.start[2] + (placement.end[2] - placement.start[2]) * ratio};

            vtkIdType pointId = findOldPoint(xyz);
            if (pointId < 0)
                pointId = findCreatedPoint(xyz);

            if (pointId < 0)
            {
                pointId = points->InsertNextPoint(xyz);
                registerCreatedPoint(pointId, xyz);
            }
            rodPointIds.push_back(pointId);
        }

        for (int segment = 0; segment < placement.segmentCount; ++segment)
        {
            const vtkIdType firstId = rodPointIds.at(segment);
            const vtkIdType secondId = rodPointIds.at(segment + 1);
            if (firstId == secondId)
            {
                return fail(QStringLiteral("第 %1 根锚杆的第 %2 段退化为零长度单元。")
                                .arg(barIndex + 1).arg(segment + 1));
            }

            double first[3]{};
            double second[3]{};
            points->GetPoint(firstId, first);
            points->GetPoint(secondId, second);
            const QString currentEdgeKey = edgeKey(first, second);
            if (occupiedEdges.contains(currentEdgeKey))
            {
                return fail(QStringLiteral("第 %1 根锚杆的第 %2 段与已有杆单元重合，已取消本次生成。")
                                .arg(barIndex + 1).arg(segment + 1));
            }
            occupiedEdges.insert(currentEdgeKey);
            vtkIdType endpoints[2] = {firstId, secondId};
            result->InsertNextCell(VTK_LINE, 2, endpoints);
        }
    }

    const vtkIdType finalPointCount = points->GetNumberOfPoints();
    if (finalPointCount < oldPointCount ||
        result->GetNumberOfCells() != oldCellCount + addedCellCount)
    {
        return fail(QStringLiteral("追加区域锚杆后的网格数量校验失败，原网格未被修改。"));
    }

    normalizeGridAttributeTupleCounts(result, false, false);

    const auto ensureIntArray = [](vtkDataSetAttributes* attributes, const char* name,
                                   vtkIdType tupleCount, int defaultValue) -> vtkDataArray*
    {
        if (vtkDataArray* existing = attributes->GetArray(name))
            return existing;
        auto created = vtkSmartPointer<vtkIntArray>::New();
        created->SetName(name);
        created->SetNumberOfComponents(1);
        created->SetNumberOfTuples(tupleCount);
        created->FillComponent(0, defaultValue);
        attributes->AddArray(created);
        return created;
    };

    vtkDataArray* nodeIds = ensureIntArray(result->GetPointData(), "node_id",
                                           finalPointCount, 0);
    int maxNodeId = 0;
    for (vtkIdType pid = 0; pid < oldPointCount; ++pid)
    {
        int nodeId = static_cast<int>(nodeIds->GetTuple1(pid));
        if (nodeId <= 0)
        {
            nodeId = static_cast<int>(pid + 1);
            nodeIds->SetTuple1(pid, nodeId);
        }
        maxNodeId = qMax(maxNodeId, nodeId);
    }
    for (vtkIdType pid = oldPointCount; pid < finalPointCount; ++pid)
        nodeIds->SetTuple1(pid, ++maxNodeId);

    vtkDataArray* matypes = ensureIntArray(result->GetCellData(), "MATYPE",
                                           oldCellCount + addedCellCount, 2);
    vtkDataArray* layers = ensureIntArray(result->GetCellData(), "LAYER",
                                          oldCellCount + addedCellCount, 1);
    vtkDataArray* ketypes = ensureIntArray(result->GetCellData(), "KETYPE",
                                           oldCellCount + addedCellCount, 0);
    vtkDataArray* elementIds = ensureIntArray(result->GetCellData(), "elem_id",
                                              oldCellCount + addedCellCount, 0);
    ensureIntArray(result->GetCellData(), "src_part", oldCellCount + addedCellCount, 0);
    ensureIntArray(result->GetCellData(), "TAIL1", oldCellCount + addedCellCount, 0);
    ensureIntArray(result->GetCellData(), "TAIL2", oldCellCount + addedCellCount, 0);
    vtkDataArray* recordIds = ensureIntArray(result->GetCellData(), "BAR_GROUP",
                                             oldCellCount + addedCellCount, 0);

    auto* recordNames = vtkStringArray::SafeDownCast(
        result->GetCellData()->GetAbstractArray("BAR_GROUP_NAME"));
    if (!recordNames)
    {
        auto created = vtkSmartPointer<vtkStringArray>::New();
        created->SetName("BAR_GROUP_NAME");
        created->SetNumberOfValues(oldCellCount + addedCellCount);
        for (vtkIdType cid = 0; cid < oldCellCount + addedCellCount; ++cid)
            created->SetValue(cid, std::string());
        result->GetCellData()->AddArray(created);
        recordNames = created;
    }
    else if (recordNames->GetNumberOfValues() < oldCellCount + addedCellCount)
    {
        const vtkIdType oldValues = recordNames->GetNumberOfValues();
        recordNames->SetNumberOfValues(oldCellCount + addedCellCount);
        for (vtkIdType cid = oldValues; cid < oldCellCount + addedCellCount; ++cid)
            recordNames->SetValue(cid, std::string());
    }

    auto* placementUuids = vtkStringArray::SafeDownCast(
        result->GetCellData()->GetAbstractArray("BAR_PLACEMENT_UUID"));
    if (!placementUuids)
    {
        auto created = vtkSmartPointer<vtkStringArray>::New();
        created->SetName("BAR_PLACEMENT_UUID");
        created->SetNumberOfValues(oldCellCount + addedCellCount);
        for (vtkIdType cid = 0; cid < oldCellCount + addedCellCount; ++cid)
            created->SetValue(cid, std::string());
        result->GetCellData()->AddArray(created);
        placementUuids = created;
    }
    else if (placementUuids->GetNumberOfValues() < oldCellCount + addedCellCount)
    {
        const vtkIdType oldValues = placementUuids->GetNumberOfValues();
        placementUuids->SetNumberOfValues(oldCellCount + addedCellCount);
        for (vtkIdType cid = oldValues; cid < oldCellCount + addedCellCount; ++cid)
            placementUuids->SetValue(cid, std::string());
    }

    int maxElementId = 0;
    for (vtkIdType cid = 0; cid < oldCellCount; ++cid)
    {
        int elementId = static_cast<int>(elementIds->GetTuple1(cid));
        if (elementId <= 0)
        {
            elementId = static_cast<int>(cid + 1);
            elementIds->SetTuple1(cid, elementId);
        }
        maxElementId = qMax(maxElementId, elementId);
    }

    const int installationLayer = -qMax(1, qAbs(firstPlacement.installationLayer));
    const QString recordName = QStringLiteral("杆单元生成记录 %1").arg(recordId);
    for (vtkIdType cid = oldCellCount; cid < oldCellCount + addedCellCount; ++cid)
    {
        matypes->SetTuple1(cid, rodMatype);
        layers->SetTuple1(cid, installationLayer);
        ketypes->SetTuple1(cid, 1);
        elementIds->SetTuple1(cid, ++maxElementId);
        recordIds->SetTuple1(cid, recordId);
        recordNames->SetValue(cid, recordName.toStdString());
        placementUuids->SetValue(cid, firstPlacement.placementUuid.toStdString());
    }

    result->Modified();
    m_importedGrid = result;
    m_hasGrid = true;
    m_sourceFileName.clear();
    if (m_importSettingsWidget)
        m_importSettingsWidget->setSourceFileName(QString());
    m_splitCacheSource = nullptr;
    m_splitCacheLine = nullptr;
    m_splitCacheNonLine = nullptr;
    syncPickMapGroupingFromGrid(m_importedGrid);
    displayCurrentGrid();
    PickManager::instance().updateStructure();
    if (m_gridEditingWidget)
        m_gridEditingWidget->setEditableGrid(m_importedGrid, QString());
    return true;
}

bool MeshModuleController::removeBarElementRecord(int recordId, QString* errMsg)
{
    const auto fail = [errMsg](const QString& message)
    {
        if (errMsg)
            *errMsg = message;
        return false;
    };
    if (!m_importedGrid || recordId <= 0)
        return fail(QStringLiteral("杆单元生成记录无效。"));
    normalizeGridAttributeTupleCounts(m_importedGrid);
    vtkDataArray* recordIds = m_importedGrid->GetCellData()->GetArray("BAR_GROUP");
    if (!recordIds)
        return fail(QStringLiteral("当前网格中没有可删除的杆单元生成记录。"));

    const vtkIdType oldPointCount = m_importedGrid->GetNumberOfPoints();
    const vtkIdType cellCount = m_importedGrid->GetNumberOfCells();
    QVector<vtkIdType> retainedCells;
    retainedCells.reserve(static_cast<int>(cellCount));
    QSet<vtkIdType> retainedPointSet;
    bool removed = false;
    for (vtkIdType cid = 0; cid < cellCount; ++cid)
    {
        vtkCell* cell = m_importedGrid->GetCell(cid);
        if (!cell || !cell->GetPointIds())
            continue;
        const bool belongsToRecord =
            cell->GetCellDimension() == 1 &&
            static_cast<int>(recordIds->GetTuple1(cid)) == recordId;
        if (belongsToRecord)
        {
            removed = true;
            continue;
        }
        retainedCells.push_back(cid);
        for (vtkIdType local = 0; local < cell->GetPointIds()->GetNumberOfIds(); ++local)
            retainedPointSet.insert(cell->GetPointIds()->GetId(local));
    }
    if (!removed)
        return fail(QStringLiteral("未找到指定的杆单元生成记录。"));

    QVector<vtkIdType> oldToNew(static_cast<int>(oldPointCount), -1);
    QVector<vtkIdType> retainedPoints;
    retainedPoints.reserve(retainedPointSet.size());
    auto points = vtkSmartPointer<vtkPoints>::New();
    points->SetDataType(m_importedGrid->GetPoints()->GetDataType());
    for (vtkIdType oldPid = 0; oldPid < oldPointCount; ++oldPid)
    {
        if (!retainedPointSet.contains(oldPid))
            continue;
        double xyz[3]{};
        m_importedGrid->GetPoint(oldPid, xyz);
        const vtkIdType newPid = points->InsertNextPoint(xyz);
        oldToNew[static_cast<int>(oldPid)] = newPid;
        retainedPoints.push_back(oldPid);
    }

    auto result = vtkSmartPointer<vtkUnstructuredGrid>::New();
    result->SetPoints(points);
    result->Allocate(retainedCells.size());
    for (vtkIdType oldCid : retainedCells)
    {
        vtkCell* cell = m_importedGrid->GetCell(oldCid);
        auto remappedIds = vtkSmartPointer<vtkIdList>::New();
        remappedIds->SetNumberOfIds(cell->GetPointIds()->GetNumberOfIds());
        for (vtkIdType local = 0; local < cell->GetPointIds()->GetNumberOfIds(); ++local)
        {
            const vtkIdType oldPid = cell->GetPointIds()->GetId(local);
            if (oldPid < 0 || oldPid >= oldToNew.size() ||
                oldToNew.at(static_cast<int>(oldPid)) < 0)
            {
                return fail(QStringLiteral("删除杆单元时节点映射失败，原网格数据可能已损坏。"));
            }
            remappedIds->SetId(local, oldToNew.at(static_cast<int>(oldPid)));
        }
        result->InsertNextCell(cell->GetCellType(), remappedIds);
    }

    result->GetFieldData()->DeepCopy(m_importedGrid->GetFieldData());
    result->GetPointData()->CopyAllocate(m_importedGrid->GetPointData(), retainedPoints.size());
    for (vtkIdType newPid = 0; newPid < retainedPoints.size(); ++newPid)
    {
        result->GetPointData()->CopyData(m_importedGrid->GetPointData(),
                                         retainedPoints.at(newPid), newPid);
    }
    result->GetCellData()->CopyAllocate(m_importedGrid->GetCellData(), retainedCells.size());
    for (vtkIdType newCid = 0; newCid < retainedCells.size(); ++newCid)
    {
        result->GetCellData()->CopyData(m_importedGrid->GetCellData(),
                                        retainedCells.at(newCid), newCid);
    }
    normalizeGridAttributeTupleCounts(result, false, false);
    result->Modified();

    m_importedGrid = result;
    m_hasGrid = m_importedGrid->GetNumberOfCells() > 0;
    m_sourceFileName.clear();
    if (m_importSettingsWidget)
        m_importSettingsWidget->setSourceFileName(QString());
    m_splitCacheSource = nullptr;
    m_splitCacheLine = nullptr;
    m_splitCacheNonLine = nullptr;
    syncPickMapGroupingFromGrid(m_importedGrid);
    displayCurrentGrid();
    PickManager::instance().updateStructure();
    if (m_gridEditingWidget)
        m_gridEditingWidget->setEditableGrid(m_importedGrid, QString());
    return true;
}

QVector<BarElementRecordInfo> MeshModuleController::barElementRecords() const
{
    QVector<BarElementRecordInfo> result;
    if (!m_importedGrid)
        return result;
    normalizeGridAttributeTupleCounts(m_importedGrid);
    vtkDataArray* recordIds = m_importedGrid->GetCellData()->GetArray("BAR_GROUP");
    if (!recordIds)
        return result;
    vtkDataArray* matypes = m_importedGrid->GetCellData()->GetArray("MATYPE");
    vtkDataArray* layers = m_importedGrid->GetCellData()->GetArray("LAYER");
    auto* placementUuids = vtkStringArray::SafeDownCast(
        m_importedGrid->GetCellData()->GetAbstractArray("BAR_PLACEMENT_UUID"));
    QMap<int, BarElementRecordInfo> byId;
    for (vtkIdType cid = 0; cid < m_importedGrid->GetNumberOfCells(); ++cid)
    {
        const int id = static_cast<int>(recordIds->GetTuple1(cid));
        vtkCell* cell = m_importedGrid->GetCell(cid);
        if (id <= 0 || !cell || cell->GetCellDimension() != 1)
            continue;
        BarElementRecordInfo& info = byId[id];
        info.id = id;
        ++info.cellCount;
        if (matypes)
            info.matype = static_cast<int>(matypes->GetTuple1(cid));
        if (layers)
            info.installationLayer = -qMax(1, qAbs(static_cast<int>(layers->GetTuple1(cid))));
        if (placementUuids && cid < placementUuids->GetNumberOfValues())
            info.placementUuid = QString::fromStdString(placementUuids->GetValue(cid));
    }
    for (auto it = byId.constBegin(); it != byId.constEnd(); ++it)
        result.push_back(it.value());
    return result;
}

bool MeshModuleController::setBarPlacementMatype(const QString& placementUuid,
                                                   int matype,
                                                   QString* errMsg)
{
    if (errMsg)
        errMsg->clear();
    if (placementUuid.trimmed().isEmpty())
    {
        if (errMsg)
            *errMsg = QStringLiteral("锚杆布置节点无效。");
        return false;
    }
    if (matype < FIRST_AUTO_MATYPE || matype > MAX_SOLVER_MATYPE)
    {
        if (errMsg)
            *errMsg = QStringLiteral("锚杆材料编号必须在 3 至 300 之间。");
        return false;
    }
    if (!m_importedGrid)
        return true;

    normalizeGridAttributeTupleCounts(m_importedGrid);
    vtkDataArray* matypes = m_importedGrid->GetCellData()->GetArray("MATYPE");
    int hostDimension = 0;
    for (vtkIdType cid = 0; cid < m_importedGrid->GetNumberOfCells(); ++cid)
    {
        if (vtkCell* cell = m_importedGrid->GetCell(cid))
            hostDimension = qMax(hostDimension, cell->GetCellDimension());
    }
    if (matypes && hostDimension >= 2)
    {
        for (vtkIdType cid = 0; cid < m_importedGrid->GetNumberOfCells() &&
                                cid < matypes->GetNumberOfTuples(); ++cid)
        {
            vtkCell* cell = m_importedGrid->GetCell(cid);
            if (cell && cell->GetCellDimension() == hostDimension &&
                static_cast<int>(matypes->GetTuple1(cid)) == matype)
            {
                if (errMsg)
                    *errMsg = QStringLiteral(
                        "材料编号 %1 已被实体网格使用。锚杆请使用独立材料编号；不同锚杆布置节点可以共用同一锚杆材料编号。")
                                  .arg(matype);
                return false;
            }
        }
    }
    auto* placementUuids = vtkStringArray::SafeDownCast(
        m_importedGrid->GetCellData()->GetAbstractArray("BAR_PLACEMENT_UUID"));
    if (!matypes || !placementUuids)
        return true; // 当前节点还没有实际生成锚杆。

    bool changed = false;
    const vtkIdType cellCount = m_importedGrid->GetNumberOfCells();
    for (vtkIdType cid = 0; cid < cellCount; ++cid)
    {
        vtkCell* cell = m_importedGrid->GetCell(cid);
        if (!cell || cell->GetCellDimension() != 1 ||
            cid >= placementUuids->GetNumberOfValues())
            continue;
        const QString uuid = QString::fromStdString(placementUuids->GetValue(cid));
        if (uuid != placementUuid)
            continue;
        if (static_cast<int>(matypes->GetTuple1(cid)) != matype)
        {
            matypes->SetTuple1(cid, matype);
            changed = true;
        }
    }

    if (!changed)
        return true;

    matypes->Modified();
    m_importedGrid->GetCellData()->Modified();
    m_importedGrid->Modified();
    syncPickMapGroupingFromGrid(m_importedGrid);
    displayCurrentGrid(false);
    PickManager::instance().updateStructure();
    if (m_gridEditingWidget)
        m_gridEditingWidget->setEditableGrid(m_importedGrid, QString());
    return true;
}

void MeshModuleController::setBarPlacementDisplayMode(bool active)
{
    if (!m_visualWidgetService || !m_importedGrid)
        return;

    if (!active)
    {
        // 重新走统一网格显示逻辑，完整恢复用户原来的颜色/筛选/不透明度设置。
        displayCurrentGrid(false);
        return;
    }

    if (!m_previewSurfaceActor || !m_previewLineActor || !m_previewLineMapper)
        displayCurrentGrid(false);

    if (m_previewSurfaceActor)
    {
        auto* prop = m_previewSurfaceActor->GetProperty();
        prop->SetOpacity(1.0);
        prop->SetColor(0.78, 0.80, 0.82);
        prop->EdgeVisibilityOn();
        prop->SetEdgeColor(0.58, 0.61, 0.64);
        prop->SetLineWidth(0.7);
        if (m_previewSurfaceMapper)
            m_previewSurfaceMapper->ScalarVisibilityOff();
    }

    if (m_previewLineActor)
    {
        auto* prop = m_previewLineActor->GetProperty();
        prop->SetOpacity(1.0);
        prop->SetLineWidth(1.8);
        prop->SetSpecular(0.0);
        prop->RenderLinesAsTubesOff();
        // 锚杆统一一种显示颜色；材料编号只作为计算属性，不再按 MATYPE 变色。
        prop->SetColor(232.0 / 255.0, 126.0 / 255.0, 48.0 / 255.0);
    }

    if (m_previewLineMapper)
        m_previewLineMapper->ScalarVisibilityOff();

    m_visualWidgetService->render();
}

void MeshModuleController::setEdgeControlRules(const QVector<EdgeControlRule>& rules)
{
    m_edgeRules = rules;
}

void MeshModuleController::setFaceControlRules(const QVector<FaceControlRule>& rules)
{
    m_faceRules = rules;
}

void MeshModuleController::setVolumeControlRules(const QVector<VolumeControlRule>& rules)
{
    m_volumeRules = rules;
}

QVector<EdgeControlRule> MeshModuleController::edgeControlRules() const
{
    return m_edgeRules;
}

QVector<FaceControlRule> MeshModuleController::faceControlRules() const
{
    return m_faceRules;
}

QVector<VolumeControlRule> MeshModuleController::volumeControlRules() const
{
    return m_volumeRules;
}
void MeshModuleController::bindSmeshPage(BasePage* page)
{
    if(auto* sub=dynamic_cast<MeshSmeshSubmeshWidget*>(page)) {
        sub->setAvailableEntities([this,sub](int dim){return availableSubmeshEntities(sub->getUuid(),dim);});
        sub->setReservedVolumeIds([this]{return sweptCadVolumeIds();});
        connect(sub,&MeshSmeshSubmeshWidget::computeRequested,this,&MeshModuleController::generateSmeshSubmesh);
        connect(sub,&MeshSmeshSubmeshWidget::configurationChanged,this,[sub]{
            auto* pm=ServiceRegistry::instance().getService<ProjectManagerService>();
            if(auto* node=pm?pm->findNodeByUuid(sub->getUuid()):nullptr)node->setText(0,sub->settings().name);
        });
    }
    if(auto* sweep=dynamic_cast<MeshSmeshSweepWidget*>(page)) {
        connect(sweep,&MeshSmeshSweepWidget::partitionRequested,this,&MeshModuleController::addSmeshPartition);
        connect(sweep,&MeshSmeshSweepWidget::computeRequested,this,[this,sweep]{
            auto* pm=ServiceRegistry::instance().getService<ProjectManagerService>();
            auto* node=pm?pm->findNodeByUuid(sweep->getUuid()):nullptr;
            if(auto* parent=node?dynamic_cast<ProjectNode*>(node->parent()):nullptr)
                if(auto* global=dynamic_cast<MeshGlobalMeshingWidget*>(parent->getPage()))global->generateSubmesh();
        });
        connect(sweep,&MeshSmeshSweepWidget::configurationChanged,this,[this,sweep]{
            auto* pm=ServiceRegistry::instance().getService<ProjectManagerService>();
            if(auto* node=pm?pm->findNodeByUuid(sweep->getUuid()):nullptr)node->setText(0,sweep->settings().name);
            refreshVolumeScopeOwnership();
        });
    }
    if(auto* partition=dynamic_cast<MeshSmeshPartitionWidget*>(page)) {
        connect(partition,&MeshSmeshPartitionWidget::logMessage,this,[](const QString& text){
            if(auto* out=ServiceRegistry::instance().getService<OutputService>())out->outputLog(text);
        });
        connect(partition,&MeshSmeshPartitionWidget::partitionChanged,this,[this,partition]{
            if(!m_gridNode)return;
            for(auto* root:m_gridNode->getChildren())for(auto* child:root->getChildren())
                if(auto* sweep=dynamic_cast<MeshSmeshSweepWidget*>(child->getPage()))
                    sweep->invalidatePartitionTasks();
        });
    }
}

QSet<int> MeshModuleController::sweptCadVolumeIds() const
{
    QSet<int> ids;
    auto* global=m_gridNode?findDirectChildByKind(m_gridNode,QStringLiteral("GlobalMeshing")):nullptr;
    if (!global) return ids;
    for (auto* child:global->getChildren()) {
        if (auto* sweep=dynamic_cast<MeshSmeshSweepWidget*>(child->getPage()))
            ids.unite(sweep->assignedCadVolumeIds());
    }
    return ids;
}

void MeshModuleController::refreshVolumeScopeOwnership()
{
    auto* global=m_gridNode?findDirectChildByKind(m_gridNode,QStringLiteral("GlobalMeshing")):nullptr;
    if (!global) return;
    for (auto* child:global->getChildren()) {
        if (auto* sub=dynamic_cast<MeshSmeshSubmeshWidget*>(child->getPage()))
            if (sub->settings().dimension==3) sub->refreshEntities();
    }
}

QStringList MeshModuleController::availableSubmeshEntities(const QString& uuid,int dimension) const
{
    auto* pre=ServiceRegistry::instance().getService<PreProcessingService>();
    auto* pm=ServiceRegistry::instance().getService<ProjectManagerService>();
    auto* node=pm?pm->findNodeByUuid(uuid):nullptr;
    if(!pre||!node)return {};
    auto* parent=dynamic_cast<ProjectNode*>(node->parent());
    if(auto* scope=parent?dynamic_cast<MeshSmeshSubmeshWidget*>(parent->getPage()):nullptr) {
        const auto settings=scope->settings();QStringList ids;
        for(int id:settings.entityIds)for(const auto& child:pre->entitySubshapeIds(settings.dimension,id,dimension))
            if(!ids.contains(child))ids.push_back(child);
        return ids;
    }
    if(auto* global=parent?dynamic_cast<MeshGlobalMeshingWidget*>(parent->getPage()):nullptr;
       global && !global->usesAllGeometry()) {
        QStringList ids;
        for(int id:global->selectedGeometryIds()) {
            if(dimension==global->meshingDimension()) ids.append(QString::number(id));
            else for(const auto& child:pre->entitySubshapeIds(global->meshingDimension(),id,dimension))
                if(!ids.contains(child))ids.append(child);
        }
        return ids;
    }
    return pre->listEntityIds(dimension);
}

void MeshModuleController::addSmeshSubmesh(int dimension,const QString& parentUuid)
{
    if(!m_gridNode||dimension<1||dimension>3)return;
    auto* pm=ServiceRegistry::instance().getService<ProjectManagerService>();
    auto* parent=pm?pm->findNodeByUuid(parentUuid):nullptr;
    if(!parent)return;
    int parentDimension=0;
    if(auto* global=dynamic_cast<MeshGlobalMeshingWidget*>(parent->getPage()))parentDimension=global->meshingDimension()+1;
    if(auto* sub=dynamic_cast<MeshSmeshSubmeshWidget*>(parent->getPage()))parentDimension=sub->settings().dimension;
    if(parentDimension<=dimension)return;
    auto* page=new MeshSmeshSubmeshWidget(dimension);
    const QString kind=dimension==3?"SmeshVolumeSubmesh":dimension==2?"SmeshFaceSubmesh":"SmeshEdgeSubmesh";
    const QString prefix=dimension==3?tr("体网格设置"):dimension==2?tr("面网格设置"):tr("边网格设置");
    QSet<QString> names;
    for(auto* child:parent->getChildren())names.insert(child->text(0));
    int number=1;
    while(names.contains(prefix+QStringLiteral("-%1").arg(number)))++number;
    page->setDisplayName(prefix+QStringLiteral("-%1").arg(number));
    bindSmeshPage(page);
    auto* node=new ProjectNode(page->settings().name,QUuid::createUuid().toString(),page);
    node->setData(0,ProjectNode::RoleUuid,node->getUuid());node->setData(0,ProjectNode::RoleKind,kind);node->setData(0,ProjectNode::RoleModule,"Mesh");parent->addChild(node);
    parent->setExpanded(true);if(pm)pm->setCurrentItem(node);
}

void MeshModuleController::addSmeshSweep()
{
    if(!m_gridNode)return;
    auto* global=findDirectChildByKind(m_gridNode,"GlobalMeshing");
    if(!global){onMeshGlobal();global=findDirectChildByKind(m_gridNode,"GlobalMeshing");}
    auto* globalPage=global?dynamic_cast<MeshGlobalMeshingWidget*>(global->getPage()):nullptr;
    if(!globalPage)return;
    if(globalPage->meshingDimension()!=3){QMessageBox::warning(nullptr,tr("扫掠网格"),tr("请先在整体剖分中选择三维体网格。"));return;}
    auto* page=new MeshSmeshSweepWidget();
    const QString prefix=tr("扫掠网格");
    QSet<QString> names;
    for(auto* child:global->getChildren())names.insert(child->text(0));
    int number=1;
    while(names.contains(prefix+QStringLiteral("-%1").arg(number)))++number;
    page->setDisplayName(prefix+QStringLiteral("-%1").arg(number));
    bindSmeshPage(page);
    auto* node=new ProjectNode(page->settings().name,QUuid::createUuid().toString(),page);
    node->setData(0,ProjectNode::RoleUuid,node->getUuid());node->setData(0,ProjectNode::RoleKind,"SmeshSweep");node->setData(0,ProjectNode::RoleModule,"Mesh");global->addChild(node);global->setExpanded(true);
    page->setMeshPlanIdentity(node->getUuid(),node->text(0));
    page->setPartitionPlanIdentity(global->getUuid());
    if(auto* pm=ServiceRegistry::instance().getService<ProjectManagerService>())pm->setCurrentItem(node);
}

void MeshModuleController::addSmeshPartition()
{
    if(!m_gridNode)return;
    auto* root=findDirectChildByKind(m_gridNode,"GlobalMeshing");
    if(!root){onMeshGlobal();root=findDirectChildByKind(m_gridNode,"GlobalMeshing");}
    auto* global=root?dynamic_cast<MeshGlobalMeshingWidget*>(root->getPage()):nullptr;
    if(!global||global->meshingDimension()!=3)return;
    auto* node=findDirectChildByKind(root,"SmeshPartition");
    if(!node) {
        auto* page=new MeshSmeshPartitionWidget();bindSmeshPage(page);
        node=new ProjectNode(tr("扫掠分块"),QUuid::createUuid().toString(),page);
        node->setData(0,ProjectNode::RoleUuid,node->getUuid());node->setData(0,ProjectNode::RoleKind,"SmeshPartition");
        node->setData(0,ProjectNode::RoleModule,"Mesh");root->addChild(node);root->setExpanded(true);
        page->setMeshPlanIdentity(root->getUuid(),root->text(0));
    }
    if(auto* pm=ServiceRegistry::instance().getService<ProjectManagerService>())pm->setCurrentItem(node);
}

void MeshModuleController::generateSmeshSubmesh(const QString& uuid)
{
    auto* pm=ServiceRegistry::instance().getService<ProjectManagerService>();auto* node=pm?pm->findNodeByUuid(uuid):nullptr;
    const bool singleVolume=node && node->data(0,ProjectNode::RoleKind).toString()=="SmeshVolumeSubmesh";
    for(auto* current=node;current;current=dynamic_cast<ProjectNode*>(current->parent()))
        if(auto* global=dynamic_cast<MeshGlobalMeshingWidget*>(current->getPage())){
            global->generateSubmesh(singleVolume?uuid:QString());
            return;
        }
}

void MeshModuleController::removeSmeshNode(const QString& uuid)
{
    auto* pm=ServiceRegistry::instance().getService<ProjectManagerService>();auto* node=pm?pm->findNodeByUuid(uuid):nullptr;
    if(!node||!node->data(0,ProjectNode::RoleKind).toString().startsWith("Smesh"))return;
    auto* parent=dynamic_cast<ProjectNode*>(node->parent());if(!parent)return;
    if(node->data(0,ProjectNode::RoleKind).toString()=="SmeshSweep") {
        // Each sweep node is an independent plan. Release its assignments
        // when removed so other sweep nodes can claim the same regions.
        MeshSmeshWorkflowState::replaceSweepTasksForPlan(node->getUuid(), {});
    }
    if(node->data(0,ProjectNode::RoleKind).toString()=="SmeshPartition") {
        MeshSmeshWorkflowState::replacePartitionRulesForPlan(parent->getUuid(),{});
        for(auto* sibling:parent->getChildren())if(auto* sweep=dynamic_cast<MeshSmeshSweepWidget*>(sibling->getPage()))
            sweep->invalidatePartitionTasks();
    }
    const bool removedSweep=node->data(0,ProjectNode::RoleKind).toString()==QStringLiteral("SmeshSweep");
    if(pm)pm->setCurrentItem(parent);parent->removeChild(node);
    std::function<void(ProjectNode*)> destroy=[&](ProjectNode* item){const auto children=item->getChildren();for(auto* child:children){item->removeChild(child);destroy(child);}if(auto* page=item->getPage()){page->beforeUnload();item->setPage(nullptr);delete page;}delete item;};destroy(node);
    if (removedSweep) refreshVolumeScopeOwnership();
}

bool MeshModuleController::collectSmeshWorkflow(const QString& meshUuid,SmeshWorkflowSettings& settings,QString& error) const
{
    settings={};settings.meshUuid=meshUuid;error.clear();
    // The Mesh module owns the GlobalMeshing node.  During project restore the
    // ProjectManager lookup tree can temporarily differ from the module's node
    // tree.  Resolve from our own tree first instead of rejecting a valid mesh.
    auto* pre=ServiceRegistry::instance().getService<PreProcessingService>();
    if(!pre){error=tr("几何模块不可用，请重新打开工程。");return false;}
    ProjectNode* root=nullptr;
    if(m_gridNode) {
        auto* candidate=findDirectChildByKind(m_gridNode, QStringLiteral("GlobalMeshing"));
        // Only one GlobalMeshing page is allowed under the Mesh root.  The
        // active module's node is more reliable than a cross-service UUID lookup.
        if(candidate && dynamic_cast<MeshGlobalMeshingWidget*>(candidate->getPage()))
            root=candidate;
    }
    if(!root) {
        auto* pm=ServiceRegistry::instance().getService<ProjectManagerService>();
        root=pm?pm->findNodeByUuid(meshUuid):nullptr;
    }
    if(!root || !dynamic_cast<MeshGlobalMeshingWidget*>(root->getPage())) {
        error=tr("整体剖分设置已失效，请重新进入该节点。");
        return false;
    }
    auto capture=[&](int dim,int id,SmeshEntityReference& ref){
        ref.dimension=dim;ref.entityId=id;
        if(id<=0||!pre->listEntityIds(dim).contains(QString::number(id))||!pre->entityCenter(dim,id,ref.center)||!pre->entityMeasure(dim,id,ref.measure)||ref.measure<=0.) {
            error=tr("几何实体 %1（维度 %2）已失效，请重新选择。").arg(id).arg(dim);return false;
        }return true;
    };
    const auto partitionRules=MeshSmeshWorkflowState::partitionRulesForPlan(root->getUuid());
    for(const auto& rule:partitionRules)if(rule.enabled) {
        SmeshPartitionSettings setting;setting.axis=rule.axis;setting.coordinate=rule.coordinate;
        setting.origin=rule.origin;setting.normal=rule.normal;
        setting.targetVolumeUiId=rule.targetVolumeUiId;setting.sourceFaceUiId=rule.sourceFaceUiId;
        if(rule.targetVolumeUiId>0&&!capture(3,rule.targetVolumeUiId,setting.target))return false;
        settings.partitions.push_back(setting);
    }
    QObject* geometryBridge=dynamic_cast<QObject*>(pre);
    const auto planes=MeshSmeshWorkflowState::planePayload(partitionRules);
    // Detect duplicate ownership before filtering out tasks with no valid
    // direction. An unconfigured row must also belong to only one node.
    QSet<QString> sweepAssignments;
    std::function<bool(ProjectNode*,QString)> walk=[&](ProjectNode* node,QString parentUuid){
        for(auto* child:node->getChildren()) {
            if(auto* page=dynamic_cast<MeshSmeshSubmeshWidget*>(child->getPage())) {
                auto sub=page->settings();sub.parentUuid=parentUuid;
                if(sub.entityIds.isEmpty()){
                    // An unused volume sizing node must not block a complete
                    // sweep mesh. It has no effect until an eligible solid is selected.
                    if(sub.dimension==3 && child->getChildren().isEmpty()) continue;
                    error=tr("“%1”尚未选择几何实体。").arg(sub.name);return false;
                }
                for(int id:sub.entityIds){SmeshEntityReference ref;if(!capture(sub.dimension,id,ref))return false;sub.entities.push_back(ref);}
                settings.submeshes.push_back(sub);if(!walk(child,sub.uuid))return false;
            } else if(auto* page=dynamic_cast<MeshSmeshSweepWidget*>(child->getPage())) {
                // All sweep nodes belong to the same conforming parent mesh.
                // A newly created, still empty node does not invalidate
                // other configured nodes. Per-node layer/size parameters are
                // carried by each sweep task, not by a global setting.
                settings.preferHex = true;
                const auto stored = MeshSmeshWorkflowState::sweepTasksFromJson(
                    page->saveJson().value(QStringLiteral("tasks")).toArray());
                for (const auto& assignment : stored) {
                    const QString regionName = assignment.partitionRegion
                        ? tr("分块 %1").arg(assignment.objectId)
                        : tr("几何体 %1").arg(assignment.objectId);
                    const QString nodeName = page->settings().name;
                    if (assignment.objectId <= 0) {
                        error=tr("“%1”包含无效区域，请重新添加扫掠区域。").arg(nodeName);
                        return false;
                    }
                    const QString key = QStringLiteral("%1:%2")
                        .arg(assignment.partitionRegion ? 1 : 0).arg(assignment.objectId);
                    if (sweepAssignments.contains(key)) {
                        error=tr("%1 被重复加入扫掠网格，请从其中一个节点移除。")
                            .arg(regionName);
                        return false;
                    }
                    sweepAssignments.insert(key);
                    // Check ALL task rows, not only page->tasks(), which
                    // deliberately filters incomplete sweep assignments.
                    // Incomplete rows must never silently become Tet regions.
                    if (assignment.sourceFaceId <= 0 || assignment.targetFaceId <= 0 ||
                        assignment.sourceFaceId == assignment.targetFaceId) {
                        error=tr("“%1”的%2没有可用的扫掠方向，请调整区域或移除任务。")
                            .arg(nodeName, regionName);
                        return false;
                    }
                    if (assignment.partitionRegion != !planes.isEmpty()) {
                        error=tr("“%1”的%2与当前分块状态不一致，请重新添加。")
                            .arg(nodeName, regionName);
                        return false;
                    }
                    if (assignment.layers <= 0 || !(assignment.sourceSize > 0.0)) {
                        error=tr("“%1”的%2网格参数无效。").arg(nodeName, regionName);
                        return false;
                    }
                    // Geometry can change without changing a numeric face ID.
                    // Recheck the exact CAD/Partition source-target pair at
                    // compute time, independent of any UI candidate cache.
                    if (!geometryBridge) {
                        error=tr("几何模块不可用，无法验证扫掠方向。");
                        return false;
                    }
                    QVariantMap analysis;
                    bool invoked = false;
                    if (assignment.partitionRegion) {
                        invoked = QMetaObject::invokeMethod(
                            geometryBridge, "externalAnalyzePartitionSweepRegion", Qt::DirectConnection,
                            Q_RETURN_ARG(QVariantMap, analysis), Q_ARG(QVariantList, planes),
                            Q_ARG(int, assignment.objectId));
                    } else {
                        invoked = QMetaObject::invokeMethod(
                            geometryBridge, "externalAnalyzeSweepVolume", Qt::DirectConnection,
                            Q_RETURN_ARG(QVariantMap, analysis), Q_ARG(int, assignment.objectId));
                    }
                    if (!invoked || !analysis.value(QStringLiteral("valid")).toBool()) {
                        error=tr("“%1”的%2无法识别扫掠方向，请检查几何。")
                            .arg(nodeName, regionName);
                        return false;
                    }
                    bool matched = false;
                    for (const QVariant& value : analysis.value(QStringLiteral("candidates")).toList()) {
                        const QVariantMap option = value.toMap();
                        if (option.value(QStringLiteral("source")).toInt() == assignment.sourceFaceId &&
                            option.value(QStringLiteral("target")).toInt() == assignment.targetFaceId) {
                            matched = true;
                            break;
                        }
                    }
                    if (!matched) {
                        error=tr("“%1”的%2扫掠方向已失效，请重新识别。")
                            .arg(nodeName, regionName);
                        return false;
                    }
                }
                const auto tasks=page->tasks();
                for(auto sweep:tasks) {
                if(sweep.partitionRegion) {
                    if(planes.isEmpty()||!geometryBridge){error=tr("扫掠分块已失效，请重新预览并选择区域。");return false;}
                    QVariantMap volume,source,target;
                    const bool ok=QMetaObject::invokeMethod(geometryBridge,"externalPartitionRegionInfo",Qt::DirectConnection,
                        Q_RETURN_ARG(QVariantMap,volume),Q_ARG(QVariantList,planes),Q_ARG(int,sweep.volumeId)) &&
                        QMetaObject::invokeMethod(geometryBridge,"externalPartitionFaceInfo",Qt::DirectConnection,
                        Q_RETURN_ARG(QVariantMap,source),Q_ARG(QVariantList,planes),Q_ARG(int,sweep.volumeId),Q_ARG(int,sweep.sourceFaceId)) &&
                        QMetaObject::invokeMethod(geometryBridge,"externalPartitionFaceInfo",Qt::DirectConnection,
                        Q_RETURN_ARG(QVariantMap,target),Q_ARG(QVariantList,planes),Q_ARG(int,sweep.volumeId),Q_ARG(int,sweep.targetFaceId));
                    if(!ok||!volume.value("valid").toBool()||!source.value("valid").toBool()||!target.value("valid").toBool()) {
                        error=tr("分块 %1 或其扫掠端面已失效，请重新选择。").arg(sweep.volumeId);return false;
                    }
                    sweep.volume={3,0,volume.value("center").value<QVector3D>(),volume.value("volume").toDouble()};
                    sweep.source={2,0,source.value("center").value<QVector3D>(),source.value("area").toDouble()};
                    sweep.target={2,0,target.value("center").value<QVector3D>(),target.value("area").toDouble()};
                    if(sweep.sourceFaceId==sweep.targetFaceId){error=tr("扫掠起始面和目标面不能相同。");return false;}
                    settings.sweeps.push_back(sweep);continue;
                }
                if(!capture(3,sweep.volumeId,sweep.volume)||!capture(2,sweep.sourceFaceId,sweep.source)||!capture(2,sweep.targetFaceId,sweep.target))return false;
                if(sweep.sourceFaceId==sweep.targetFaceId){error=tr("“%1”的起始面和目标面不能相同。").arg(sweep.name);return false;}
                const auto faces=pre->entitySubshapeIds(3,sweep.volumeId,2);
                if(!faces.contains(QString::number(sweep.sourceFaceId))||!faces.contains(QString::number(sweep.targetFaceId))){error=tr("“%1”的起始面和目标面必须属于所选体。").arg(sweep.name);return false;}
                settings.sweeps.push_back(sweep);
                }
            }
        }return true;
    };
    settings.meshUuid=root->getUuid();
    if (!walk(root,root->getUuid())) return false;
    // Catch saved configurations from older versions as well as selections
    // made before the sweep node was created. Native meshing must never
    // silently discard a conflicting volume sizing scope.
    QSet<int> sweepCadIds;
    for(const auto& sweep:settings.sweeps)
        if(!sweep.partitionRegion) sweepCadIds.insert(sweep.volumeId);
    for(const auto& sub:settings.submeshes) {
        if(sub.dimension!=3) continue;
        for(int id:sub.entityIds) {
            if(sweepCadIds.contains(id)) {
                error=tr("几何体 %1 已用于扫掠网格，不能同时加入“%2”。请打开体网格设置刷新选择。")
                    .arg(id).arg(sub.name);
                return false;
            }
        }
    }
    // A CAD face genuinely shared between two sweep volumes must have a
    // compatible extrusion role. This is a conservative, topology-backed
    // precheck only; OCC Partition may introduce additional shared faces,
    // which the native backend validates again after Partition.
    QHash<QString, QVector<const SmeshSweepSettings*>> ownersByFace;
    for (const auto& sweep : settings.sweeps) {
        if (sweep.partitionRegion) continue;
        // Faces on the original unpartitioned geometry can be checked here;
        // generated Partition faces are checked again by the native mesher.
        QSet<QString> uniqueFaces;
        for (const QString& face : pre->entitySubshapeIds(3, sweep.volumeId, 2))
            uniqueFaces.insert(face);
        for (const QString& face : uniqueFaces)
            ownersByFace[face].push_back(&sweep);
    }
    for (auto it=ownersByFace.cbegin(); it!=ownersByFace.cend(); ++it) {
        const auto& owners=it.value();
        if (owners.size()<2) continue;
        const int faceId=it.key().toInt();
        for (int i=0; i<owners.size(); ++i) {
            const auto& first=*owners[i];
            const QVector3D dirA=first.target.center-first.source.center;
            for (int j=i+1; j<owners.size(); ++j) {
                const auto& second=*owners[j];
                const QVector3D dirB=second.target.center-second.source.center;
                const bool capA=faceId==first.sourceFaceId||faceId==first.targetFaceId;
                const bool capB=faceId==second.sourceFaceId||faceId==second.targetFaceId;
                if (dirA.lengthSquared()>1.e-12f && dirB.lengthSquared()>1.e-12f) {
                    const double alignment=std::abs(double(QVector3D::dotProduct(dirA.normalized(),dirB.normalized())));
                    if (alignment<0.98 || capA!=capB) {
                        error=tr("扫掠区域 %1 与 %2 的共享面 %3 不兼容，请调整方向或分组。")
                            .arg(first.volumeId).arg(second.volumeId).arg(faceId);
                        return false;
                    }
                }
                // On a shared side face, the along-sweep edge subdivision
                // must agree. Stacked bodies sharing a cap can have different
                // layer counts and therefore remain independent nodes.
                if (!capA && !capB && first.layers!=second.layers) {
                    error=tr("扫掠区域 %1 与 %2 共用侧面，请将扫掠层数设为一致。")
                        .arg(first.volumeId).arg(second.volumeId);
                    return false;
                }
            }
        }
    }
    return true;
}
