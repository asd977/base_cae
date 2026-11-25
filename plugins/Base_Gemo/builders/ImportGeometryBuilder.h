#pragma once

#include <QString>
#include <QVector>

enum class StepImportSplitMode
{
    WholeModel = 0,
    Part = 1,
    Body = 2
};

struct StepImportOptions
{
    StepImportSplitMode splitMode = StepImportSplitMode::Part;
    bool preserveColors = true;
    bool includeHidden = false;
    bool groupByAssembly = true;
};

#include <TopoDS_Shape.hxx>

struct StepImportPartInfo
{
    int partIndex = -1;
    QString uuid;
    QString sourceKey;
    QString name;
    QString assemblyPath;
    QString colorHex;
    bool visible = true;
    TopoDS_Shape shape;

    bool isValid() const
    {
        return partIndex >= 0 && !uuid.isEmpty() && !shape.IsNull();
    }
};

struct DxfGeometryRecord
{
    QString layer;
    QString handle;
    QString type;
    TopoDS_Shape shape;
};

class ImportGeometryBuilder
{
public:
    // 导入页和工程恢复共用的 STEP 选项。选项按导入节点 UUID 缓存，
    // 不改变 IGeometryService 现有接口，避免影响其他模块。
    static void setStepImportOptions(const QString& importUuid,
                                     const StepImportOptions& options);
    static StepImportOptions stepImportOptions(const QString& importUuid);
    static void clearStepImportOptions(const QString& importUuid);

    static bool recreateImportGeometry(const QString& uuid,
                                       const QString& name,
                                       const QString& filePath,
                                       QString& errorMsg);

    // 几何、草图和等高线统一使用 GDAL 读取 DXF。
    // GDAL 处理图块和 OCS；圆弧、样条以离散折线导入。
    static bool readDxfShape(const QString& filePath,
                             TopoDS_Shape& shape,
                             QString& errorMsg);
    // 保留 GDAL 展开后的对象、图层和三维坐标。
    static bool readDxfRecords(const QString& filePath, QVector<DxfGeometryRecord>& records,
                               QString& diagnostic);

    // STEP/XDE 导入会把装配中的最终零件和多实体模型拆分为可单独操作的 Shape。
    // 结果按父导入节点 UUID 缓存在内存中，供 GeometryWorkbench 创建 STEP 实体节点。
    static const QVector<StepImportPartInfo>& stepImportParts(const QString& importUuid);

    // STEP 文件重新读取后标记为“待同步”。模型树的普通刷新不应重复
    // 注册和三角化全部子实体，否则大型 STEP 会反复卡顿。
    static bool stepImportNeedsSynchronization(const QString& importUuid);
    static void markStepImportSynchronized(const QString& importUuid);

    // 工程重开时按 sourceKey、装配路径和名称匹配，partIndex 仅作兼容回退。
    static bool recreateStepImportPart(const QString& uuid,
                                       const QString& name,
                                       const QString& importUuid,
                                       int partIndex,
                                       const QString& sourceKey,
                                       const QString& originalName,
                                       const QString& assemblyPath,
                                       const QString& colorHex,
                                       QString& errorMsg);

    // 将 STEP 中识别到的实体颜色应用到当前显示对象；颜色为空时保持统一默认样式。
    static void applyStepColor(const QString& uuid, const QString& colorHex);

    static void clearStepImportParts(const QString& importUuid);
};
