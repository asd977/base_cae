#include "PostResultGenerator.h"
#include "PostTecplotUtils.h"

#include <PublicConstants.h>

#include <QDir>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QDirIterator>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QStringConverter>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTextStream>

#include <vtkCellData.h>
#include <vtkCellType.h>
#include <vtkCompositeDataSet.h>
#include <vtkDataArray.h>
#include <vtkDoubleArray.h>
#include <vtkFieldData.h>
#include <vtkDataSetAttributes.h>
#include <vtkInformation.h>
#include <vtkIntArray.h>
#include <vtkMultiBlockDataSet.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkSmartPointer.h>
#include <vtkUnstructuredGrid.h>
#include <vtkXMLMultiBlockDataWriter.h>
#include <vtkXMLUnstructuredGridWriter.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
struct Node
{
    int id = 0;
    QVector<double> xyz;
};

struct Element
{
    int id = 0;
    QVector<int> nodes;
    int type = 0;             // 后处理实际输出的拓扑类型
    int sourceType = 0;       // 原始网格 KETYPE，拆分高阶单元后仍保持不变
    int material = 1;
    int layer = 1;
};

struct Mesh
{
    int dimension = 0;
    int nodeCount = 0;
    int elementCount = 0;
    int nodesPerElement = 0;
    int materialColumns = 0;
    QVector<Node> nodes;
    QVector<Element> elements;
    QHash<int, int> nodeIdToIndex;
};

struct ZoneGroup
{
    QString name;
    QSet<int> materials;
};

struct FieldSource
{
    QString path;
    QString suffix;
};

struct RuptureFlags
{
    bool shear = false;
    bool tensile = false;
};

bool filesHaveSameContents(const QString& leftPath, const QString& rightPath)
{
    const QFileInfo leftInfo(leftPath);
    const QFileInfo rightInfo(rightPath);
    if (!leftInfo.exists() || !rightInfo.exists() || leftInfo.size() != rightInfo.size())
        return false;

    QFile left(leftPath);
    QFile right(rightPath);
    if (!left.open(QIODevice::ReadOnly) || !right.open(QIODevice::ReadOnly))
        return false;

    constexpr qint64 chunkSize = 64 * 1024;
    while (!left.atEnd() && !right.atEnd())
    {
        if (left.read(chunkSize) != right.read(chunkSize))
            return false;
    }
    return left.atEnd() && right.atEnd();
}

QVector<QString> splitFields(const QString& line)
{
    QVector<QString> out;
    const QStringList parts = line.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    out.reserve(parts.size());
    for (const QString& part : parts)
        out.push_back(part);
    return out;
}

bool readNextNonEmptyLine(QTextStream& in, QString& line)
{
    while (!in.atEnd())
    {
        line = in.readLine();
        if (!line.trimmed().isEmpty())
            return true;
    }
    return false;
}

QString existingFile(const QDir& dir, const QString& name)
{
    const QFileInfo exact(dir.filePath(name));
    if (exact.exists() && exact.isFile())
        return exact.absoluteFilePath();

    const QFileInfoList entries = dir.entryInfoList(QDir::Files);
    for (const QFileInfo& info : entries)
    {
        if (QString::compare(info.fileName(), name, Qt::CaseInsensitive) == 0)
            return info.absoluteFilePath();
    }
    return QString();
}

QString findInResultOrInput(const QDir& resultDir, const QString& projectDir, const QString& name)
{
    QString path = existingFile(resultDir, name);
    if (!path.isEmpty())
        return path;

    if (!projectDir.trimmed().isEmpty())
    {
        const QDir root(projectDir);
        const QStringList inputDirectories = {
            root.filePath(QString::fromUtf8(PublicConstants::kAnalysisInputDirectoryName)),
            root.filePath(QString::fromUtf8(PublicConstants::kProjectInputDirectoryName)),
            root.filePath(QString::fromLatin1(PublicConstants::kLegacyInputDirectoryName))};
        for (const QString& inputDirectory : inputDirectories)
        {
            path = existingFile(QDir(inputDirectory), name);
            if (!path.isEmpty())
                break;
        }
    }
    return path;
}

QString findInOriginalInput(const QDir& resultDir, const QString& projectDir,
                            const QString& name)
{
    QStringList inputDirectories;

    // GEOSTR is stored directly below the analysis workspace. Prefer that
    // workspace's calculation input because it still carries the original
    // excavation material/layer markers. The solver-normalized IIIN files in
    // GEOSTR intentionally restore excavated cells to the intact rock state.
    QDir analysisWorkspace(resultDir);
    if (analysisWorkspace.cdUp())
    {
        inputDirectories.push_back(analysisWorkspace.filePath(
            QString::fromUtf8(PublicConstants::kAnalysisInputDirectoryName)));
    }

    if (!projectDir.trimmed().isEmpty())
    {
        const QDir root(projectDir);
        inputDirectories.push_back(root.filePath(
            QString::fromUtf8(PublicConstants::kAnalysisInputDirectoryName)));
        inputDirectories.push_back(root.filePath(
            QString::fromUtf8(PublicConstants::kProjectInputDirectoryName)));
        inputDirectories.push_back(root.filePath(
            QString::fromLatin1(PublicConstants::kLegacyInputDirectoryName)));
    }

    QSet<QString> visited;
    for (const QString& inputDirectory : std::as_const(inputDirectories))
    {
        const QString key = QDir::cleanPath(inputDirectory).toCaseFolded();
        if (visited.contains(key))
            continue;
        visited.insert(key);
        const QString path = existingFile(QDir(inputDirectory), name);
        if (!path.isEmpty())
            return path;
    }
    return QString();
}

bool parseMesh(const QString& filePath, Mesh& mesh, QString* error)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        if (error)
            *error = QStringLiteral("无法打开计算网格：%1").arg(filePath);
        return false;
    }

    QTextStream in(&file);
    QString line;
    if (!readNextNonEmptyLine(in, line))
    {
        if (error)
            *error = QStringLiteral("计算网格为空：%1").arg(filePath);
        return false;
    }

    const QVector<QString> header = splitFields(line);
    if (header.size() < 5)
    {
        if (error)
            *error = QStringLiteral("计算网格首行格式不完整：%1").arg(filePath);
        return false;
    }

    bool ok = false;
    mesh.nodeCount = header.at(0).toInt(&ok);
    mesh.dimension = header.at(1).toInt();
    mesh.elementCount = header.at(2).toInt();
    mesh.nodesPerElement = header.at(3).toInt();
    mesh.materialColumns = header.at(4).toInt();
    if (!ok || mesh.nodeCount <= 0 || mesh.elementCount <= 0 ||
        (mesh.dimension != 2 && mesh.dimension != 3) || mesh.nodesPerElement <= 0 ||
        mesh.materialColumns < 2)
    {
        if (error)
            *error = QStringLiteral("计算网格基本参数无效：%1").arg(filePath);
        return false;
    }

    if (!readNextNonEmptyLine(in, line))
    {
        if (error)
            *error = QStringLiteral("计算网格缺少控制行：%1").arg(filePath);
        return false;
    }

    mesh.nodes.clear();
    mesh.nodes.reserve(mesh.nodeCount);
    mesh.nodeIdToIndex.clear();
    for (int i = 0; i < mesh.nodeCount; ++i)
    {
        if (!readNextNonEmptyLine(in, line))
        {
            if (error)
                *error = QStringLiteral("计算网格节点数据不足：%1").arg(filePath);
            return false;
        }

        const QVector<QString> fields = splitFields(line);
        if (fields.size() < mesh.dimension + 1)
        {
            if (error)
                *error = QStringLiteral("计算网格节点行格式错误：%1").arg(line);
            return false;
        }

        Node node;
        node.xyz = {fields.at(0).toDouble(), fields.at(1).toDouble(),
                    mesh.dimension == 3 ? fields.at(2).toDouble() : 0.0};
        node.id = fields.at(mesh.dimension).toInt();
        if (node.id <= 0)
            node.id = i + 1;
        mesh.nodeIdToIndex.insert(node.id, mesh.nodes.size());
        mesh.nodes.push_back(node);
    }

    mesh.elements.clear();
    mesh.elements.reserve(mesh.elementCount);
    for (int i = 0; i < mesh.elementCount; ++i)
    {
        if (!readNextNonEmptyLine(in, line))
            break;

        const QVector<QString> fields = splitFields(line);
        const int typeIndex = 1 + mesh.nodesPerElement;
        const int materialIndex = typeIndex + 1;
        if (fields.size() <= materialIndex)
            continue;

        Element element;
        element.id = fields.at(0).toInt();
        element.nodes.reserve(mesh.nodesPerElement);
        for (int j = 0; j < mesh.nodesPerElement; ++j)
            element.nodes.push_back(fields.at(j + 1).toInt());
        element.type = fields.at(typeIndex).toInt();
        element.sourceType = element.type;
        element.material = fields.at(materialIndex).toInt();
        const int layerIndex = materialIndex + 1;
        if (fields.size() > layerIndex)
            element.layer = fields.at(layerIndex).toInt();
        if (element.type > 0 && element.material > 0)
            mesh.elements.push_back(element);
    }

    if (mesh.elements.size() != mesh.elementCount && error)
        *error = QStringLiteral("计算网格单元数据不足：应有 %1，实际读取 %2")
                     .arg(mesh.elementCount)
                     .arg(mesh.elements.size());
    return !mesh.nodes.isEmpty() && mesh.elements.size() == mesh.elementCount;
}

int nodeAt(const Element& element, int oneBasedPosition)
{
    const int index = oneBasedPosition - 1;
    return index >= 0 && index < element.nodes.size() ? element.nodes.at(index) : 0;
}

Element makeExpandedElement(const Element& source, int type,
                            std::initializer_list<int> positions)
{
    Element element;
    element.id = source.id;
    element.type = type;
    element.sourceType = source.sourceType > 0 ? source.sourceType : source.type;
    element.material = source.material;
    element.layer = source.layer;
    element.nodes.reserve(static_cast<int>(positions.size()));
    for (int position : positions)
        element.nodes.push_back(nodeAt(source, position));
    return element;
}

void appendExpanded(const Element& source, QVector<Element>& out)
{
    auto add = [&](int type, std::initializer_list<int> positions)
    {
        out.push_back(makeExpandedElement(source, type, positions));
    };

    switch (source.type)
    {
    case 1: add(1, {1, 2}); break;
    case 2: add(1, {1, 2}); add(1, {2, 3}); break;
    case 3: case 38: add(3, {1, 2, 3, 4}); break;
    case 4: add(3, {1, 2, 5, 6}); add(3, {2, 3, 4, 5}); break;
    case 5:
        add(31, {1, 2, 8}); add(31, {2, 4, 8}); add(31, {2, 3, 4});
        add(31, {8, 6, 7}); add(31, {8, 4, 6}); add(31, {4, 5, 6});
        break;
    case 6: add(6, {1, 2, 3, 4, 5, 6, 7, 8}); break;
    case 7: case 8: case 9: case 17:
        out.push_back(source);
        break;
    case 10: case 21: add(1, {1, 2}); break;
    case 11: add(3, {1, 2, 3, 4}); break;
    case 12:
        add(31, {1, 2, 5}); add(31, {2, 3, 5});
        add(31, {3, 4, 5}); add(31, {4, 1, 5});
        break;
    case 13: add(6, {1, 2, 3, 4, 5, 6, 7, 8}); break;
    case 14:
        add(1, {1, 2}); add(1, {2, 3}); add(1, {3, 4});
        break;
    case 15:
        add(1, {1, 2}); add(1, {2, 3}); add(1, {3, 4}); add(1, {4, 5});
        break;
    case 29:
        add(31, {1, 9, 8}); add(31, {1, 2, 9}); add(31, {2, 3, 9});
        add(31, {3, 4, 9}); add(31, {8, 9, 7}); add(31, {9, 6, 7});
        add(31, {9, 4, 5}); add(31, {9, 5, 6});
        break;
    case 31: add(31, {1, 2, 3}); break;
    case 32:
        add(31, {1, 2, 6}); add(31, {2, 4, 6});
        add(31, {2, 3, 4}); add(31, {6, 4, 5});
        break;
    case 33:
        add(31, {1, 10, 9}); add(31, {1, 2, 10}); add(31, {2, 5, 10});
        add(31, {2, 3, 5}); add(31, {3, 4, 5}); add(31, {9, 6, 8});
        add(31, {9, 10, 6}); add(31, {10, 5, 6}); add(31, {8, 6, 7});
        break;
    case 34:
        add(31, {1, 13, 12}); add(31, {1, 2, 13}); add(31, {2, 14, 13});
        add(31, {2, 3, 14}); add(31, {3, 6, 14}); add(31, {3, 4, 6});
        add(31, {4, 5, 6}); add(31, {12, 15, 11}); add(31, {12, 13, 15});
        add(31, {13, 7, 15}); add(31, {13, 14, 7}); add(31, {14, 6, 7});
        add(31, {11, 8, 10}); add(31, {11, 15, 8}); add(31, {15, 7, 8});
        add(31, {10, 8, 9});
        break;
    case 35: add(35, {1, 2, 3, 4}); break;
    case 36: add(6, {1, 2, 3, 4, 10, 11, 12, 13}); break;
    case 37: add(6, {1, 3, 5, 7, 13, 15, 17, 19}); break;
    default: break;
    }
}

struct ExcavationVisibility
{
    bool enabled = false;
    bool fullyExcavated = false;
    int completedLevel = 0;
};

ExcavationVisibility excavationVisibility(const QDir& resultDir)
{
    ExcavationVisibility visibility;
    const QString leaf = QFileInfo(resultDir.absolutePath()).fileName().toUpper();
    static const QRegularExpression fullExpression(
        QStringLiteral("[EJ]ALL$"), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch fullMatch = fullExpression.match(leaf);
    if (fullMatch.hasMatch() &&
        (fullMatch.capturedStart() == 0 || leaf.startsWith(QStringLiteral("CASE"))))
    {
        visibility.enabled = true;
        visibility.fullyExcavated = true;
        return visibility;
    }

    static const QRegularExpression stageExpression(
        QStringLiteral("[EJ](\\d+)$"), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = stageExpression.match(leaf);
    if (match.hasMatch() &&
        (match.capturedStart() == 0 || leaf.startsWith(QStringLiteral("CASE"))))
    {
        visibility.enabled = true;
        visibility.completedLevel = match.captured(1).toInt();
    }
    return visibility;
}

bool isVisibleAtExcavationStage(const Element& element,
                                const ExcavationVisibility& visibility)
{
    if (!visibility.enabled || element.layer >= 0)
        return true;
    if (visibility.fullyExcavated)
        return false;
    return -element.layer > visibility.completedLevel;
}

QVector<Element> expandElements(const Mesh& mesh, const QDir& resultDir,
                                bool includeExcavated = false)
{
    QVector<Element> expanded;
    expanded.reserve(mesh.elements.size());
    const ExcavationVisibility visibility = excavationVisibility(resultDir);
    for (const Element& element : mesh.elements)
    {
        if (element.material != 1 &&
            (includeExcavated || isVisibleAtExcavationStage(element, visibility)))
            appendExpanded(element, expanded);
    }
    return expanded;
}

double safeValue(const QVector<double>& row, int index);
bool isJointType(int type);

int topologyNodeCount(int type)
{
    switch (type)
    {
    case 1: return 2;
    case 31: return 3;
    case 3: case 35: case 38: return 4;
    case 6: return 8;
    default: return 0;
    }
}

QString topologyName(int type)
{
    switch (type)
    {
    case 1: return QStringLiteral("LINESEG");
    case 31: return QStringLiteral("TRIANGLE");
    case 3: case 38: return QStringLiteral("QUADRILATERAL");
    case 35: return QStringLiteral("TETRAHEDRON");
    case 6: return QStringLiteral("BRICK");
    default: return QString();
    }
}

int vtkTopologyType(int type)
{
    switch (type)
    {
    case 31: return VTK_TRIANGLE;
    case 3: case 38: return VTK_QUAD;
    case 35: return VTK_TETRA;
    case 6: return VTK_HEXAHEDRON;
    default: return VTK_EMPTY_CELL;
    }
}

constexpr int kAllMaterials = std::numeric_limits<int>::min();

QVector<const Element*> displayElements(const QVector<Element>& expanded,
                                        const QSet<int>& maskedMaterials,
                                        int requiredMaterial = kAllMaterials)
{
    QVector<const Element*> selected;
    selected.reserve(expanded.size());
    for (const Element& element : expanded)
    {
        if (maskedMaterials.contains(element.material) || isJointType(element.type))
            continue;
        if (requiredMaterial != kAllMaterials && element.material != requiredMaterial)
            continue;
        if (vtkTopologyType(element.type) == VTK_EMPTY_CELL)
            continue;
        selected.push_back(&element);
    }
    return selected;
}

vtkSmartPointer<vtkUnstructuredGrid> buildVtuGrid(
    const Mesh& mesh, const QVector<Element>& expanded,
    const QSet<int>& maskedMaterials, const QStringList& variables,
    const QVector<QVector<double>>* pointValues,
    const QHash<int, QVector<double>>* cellValues, QString* error,
    int requiredMaterial = kAllMaterials)
{
    const QVector<const Element*> selected =
        displayElements(expanded, maskedMaterials, requiredMaterial);
    if (selected.isEmpty())
    {
        if (error)
            *error = QStringLiteral("没有可写入后处理网格的有效面/体单元。");
        return nullptr;
    }

    QSet<int> usedNodeIds;
    for (const Element* element : selected)
    {
        const int nodeCount = topologyNodeCount(element->type);
        for (int i = 0; i < nodeCount; ++i)
        {
            const int nodeId = element->nodes.value(i);
            if (mesh.nodeIdToIndex.contains(nodeId))
                usedNodeIds.insert(nodeId);
        }
    }

    auto grid = vtkSmartPointer<vtkUnstructuredGrid>::New();
    auto points = vtkSmartPointer<vtkPoints>::New();
    points->SetDataTypeToDouble();

    auto nodeIds = vtkSmartPointer<vtkIntArray>::New();
    nodeIds->SetName("node_id");
    nodeIds->SetNumberOfComponents(1);

    QVector<vtkSmartPointer<vtkDoubleArray>> pointArrays;
    if (pointValues)
    {
        pointArrays.reserve(variables.size());
        for (const QString& variable : variables)
        {
            auto array = vtkSmartPointer<vtkDoubleArray>::New();
            const QByteArray name = variable.toUtf8();
            array->SetName(name.constData());
            array->SetNumberOfComponents(1);
            pointArrays.push_back(array);
        }
    }

    // 每个 MATYPE Block 内只按 solver node_id 建立一次节点。不同材料的接口由
    // VTM Block 天然分离；LAYER 只是单元属性，不再人为复制节点或拆分工程域。
    QHash<int, vtkIdType> localNodeIds;
    localNodeIds.reserve(usedNodeIds.size());
    for (const Node& node : mesh.nodes)
    {
        if (!usedNodeIds.contains(node.id))
            continue;

        const double x = node.xyz.value(0);
        const double y = node.xyz.value(1);
        const double z = mesh.dimension == 3 ? node.xyz.value(2) : 0.0;
        const vtkIdType localId = points->InsertNextPoint(x, y, z);
        localNodeIds.insert(node.id, localId);
        nodeIds->InsertNextValue(node.id);

        if (pointValues)
        {
            const int globalIndex = mesh.nodeIdToIndex.value(node.id, -1);
            const QVector<double> row =
                globalIndex >= 0 && globalIndex < pointValues->size()
                    ? pointValues->at(globalIndex)
                    : QVector<double>{};
            for (int variable = 0; variable < pointArrays.size(); ++variable)
                pointArrays[variable]->InsertNextValue(safeValue(row, variable));
        }
    }

    grid->SetPoints(points);
    grid->GetPointData()->AddArray(nodeIds);
    for (const auto& array : pointArrays)
        grid->GetPointData()->AddArray(array);

    auto ketype = vtkSmartPointer<vtkIntArray>::New();
    auto matype = vtkSmartPointer<vtkIntArray>::New();
    auto layer = vtkSmartPointer<vtkIntArray>::New();
    auto elemId = vtkSmartPointer<vtkIntArray>::New();
    auto topology = vtkSmartPointer<vtkIntArray>::New();
    ketype->SetName("KETYPE");
    matype->SetName("MATYPE");
    layer->SetName("LAYER");
    elemId->SetName("elem_id");
    topology->SetName("TOPOLOGY_TYPE");

    QVector<vtkSmartPointer<vtkDoubleArray>> cellArrays;
    if (cellValues)
    {
        cellArrays.reserve(variables.size());
        for (const QString& variable : variables)
        {
            auto array = vtkSmartPointer<vtkDoubleArray>::New();
            const QByteArray name = variable.toUtf8();
            array->SetName(name.constData());
            array->SetNumberOfComponents(1);
            cellArrays.push_back(array);
        }
    }

    grid->Allocate(selected.size());
    for (const Element* element : selected)
    {
        const int vtkType = vtkTopologyType(element->type);
        const int nodeCount = topologyNodeCount(element->type);
        QVector<vtkIdType> ids;
        ids.reserve(nodeCount);
        bool complete = true;
        for (int i = 0; i < nodeCount; ++i)
        {
            const auto it = localNodeIds.constFind(element->nodes.value(i));
            if (it == localNodeIds.constEnd())
            {
                complete = false;
                break;
            }
            ids.push_back(it.value());
        }
        if (!complete || ids.size() != nodeCount)
            continue;

        grid->InsertNextCell(vtkType, static_cast<vtkIdType>(ids.size()), ids.constData());
        ketype->InsertNextValue(element->sourceType > 0 ? element->sourceType : element->type);
        matype->InsertNextValue(element->material);
        layer->InsertNextValue(element->layer);
        elemId->InsertNextValue(element->id);
        topology->InsertNextValue(vtkType);

        if (cellValues)
        {
            const QVector<double> row = cellValues->value(element->id);
            for (int variable = 0; variable < cellArrays.size(); ++variable)
                cellArrays[variable]->InsertNextValue(safeValue(row, variable));
        }
    }

    if (grid->GetNumberOfCells() <= 0 || grid->GetNumberOfPoints() <= 0)
    {
        if (error)
            *error = QStringLiteral("构造出的后处理网格为空。");
        return nullptr;
    }

    grid->GetCellData()->AddArray(ketype);
    grid->GetCellData()->AddArray(matype);
    grid->GetCellData()->AddArray(layer);
    grid->GetCellData()->AddArray(elemId);
    grid->GetCellData()->AddArray(topology);
    for (const auto& array : cellArrays)
        grid->GetCellData()->AddArray(array);

    grid->Squeeze();
    return grid;
}

void markNormalizedTopology(vtkUnstructuredGrid* grid)
{
    if (!grid || !grid->GetFieldData())
        return;
    grid->GetFieldData()->RemoveArray("CAE_POST_TOPOLOGY_NORMALIZED");
    auto marker = vtkSmartPointer<vtkIntArray>::New();
    marker->SetName("CAE_POST_TOPOLOGY_NORMALIZED");
    marker->SetNumberOfComponents(1);
    marker->InsertNextValue(4);
    grid->GetFieldData()->AddArray(marker);
}

bool writeVtuGrid(const QString& filePath, vtkUnstructuredGrid* grid, QString* error)
{
    if (!grid || grid->GetNumberOfPoints() <= 0 || grid->GetNumberOfCells() <= 0)
    {
        if (error)
            *error = QStringLiteral("后处理 VTU 网格为空：%1").arg(filePath);
        return false;
    }

    // VTK 9.2 在部分 Windows 环境中对非 ASCII 文件路径支持不稳定。
    // 统一写入 ASCII 临时路径，再由 Qt 复制到工程目录，保证中文工程路径可靠。
    QTemporaryFile temporary(
        QDir(QDir::tempPath()).filePath(QStringLiteral("cae_post_write_XXXXXX.vtu")));
    temporary.setAutoRemove(true);
    if (!temporary.open())
    {
        if (error)
            *error = QStringLiteral("无法创建 VTU 临时文件：%1").arg(filePath);
        return false;
    }
    const QString temporaryPath = temporary.fileName();
    temporary.close();

    // 新缓存写出前已完成退化拓扑恢复。marker>=4 表示所有重复节点 Hex
    // 都已按原槽位连接拆成标准 Tet，或在零体积时被丢弃。
    markNormalizedTopology(grid);

    auto writer = vtkSmartPointer<vtkXMLUnstructuredGridWriter>::New();
    writer->SetInputData(grid);
    const QByteArray encodedPath = temporaryPath.toLocal8Bit();
    writer->SetFileName(encodedPath.constData());

    // 内部后处理缓存优先首次生成/读取速度：raw appended binary 比 ASCII Tecplot
    // 解析开销低得多，也避免多 Zone 再 Append。缓存是可重建的，不需要为体积
    // 牺牲交互速度做高压缩。
    writer->SetDataModeToAppended();
    writer->EncodeAppendedDataOff();
    writer->SetCompressorTypeToNone();
    if (writer->Write() != 1)
    {
        if (error)
            *error = QStringLiteral("写入后处理 VTU 失败：%1").arg(filePath);
        return false;
    }

    QFile::remove(filePath);
    if (!QFile::copy(temporaryPath, filePath))
    {
        if (error)
            *error = QStringLiteral("无法复制后处理 VTU 到工程目录：%1").arg(filePath);
        return false;
    }

    // 不再生成 surface.vtp / domains.vtp 两套旁路缓存。
    QFile::remove(PostTecplotUtils::surfaceCachePath(filePath));
    QFile::remove(PostTecplotUtils::domainSurfaceCachePath(filePath));
    return true;
}

bool copyDirectoryContents(const QString& sourcePath, const QString& targetPath,
                           QString* error)
{
    const QDir source(sourcePath);
    if (!source.exists())
    {
        if (error)
            *error = QStringLiteral("VTM 临时数据目录不存在：%1").arg(sourcePath);
        return false;
    }

    QDir target(targetPath);
    if (!target.exists() && !target.mkpath(QStringLiteral(".")))
    {
        if (error)
            *error = QStringLiteral("无法创建 VTM 数据目录：%1").arg(targetPath);
        return false;
    }

    QDirIterator iterator(sourcePath, QDir::AllEntries | QDir::NoDotAndDotDot,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext())
    {
        const QString sourceEntry = iterator.next();
        const QFileInfo sourceInfo(sourceEntry);
        const QString relative = source.relativeFilePath(sourceEntry);
        const QString targetEntry = target.filePath(relative);
        if (sourceInfo.isDir())
        {
            if (!target.mkpath(relative))
            {
                if (error)
                    *error = QStringLiteral("无法创建 VTM 子目录：%1").arg(targetEntry);
                return false;
            }
            continue;
        }

        if (!QDir().mkpath(QFileInfo(targetEntry).absolutePath()))
        {
            if (error)
                *error = QStringLiteral("无法创建 VTM 文件目录：%1").arg(targetEntry);
            return false;
        }
        QFile::remove(targetEntry);
        if (!QFile::copy(sourceEntry, targetEntry))
        {
            if (error)
                *error = QStringLiteral("无法复制 VTM 数据文件：%1").arg(targetEntry);
            return false;
        }
    }
    return true;
}

vtkSmartPointer<vtkMultiBlockDataSet> buildMaterialBlocks(
    const Mesh& mesh, const QVector<Element>& expanded,
    const QSet<int>& maskedMaterials, const QStringList& variables,
    const QVector<QVector<double>>* pointValues,
    const QHash<int, QVector<double>>* cellValues, QString* error)
{
    // 单次扫描完成 MATYPE 分组，避免每个材料都重新遍历整张网格。
    QMap<int, QVector<Element>> elementsByMaterial;
    for (const Element* element : displayElements(expanded, maskedMaterials))
        elementsByMaterial[element->material].push_back(*element);

    if (elementsByMaterial.isEmpty())
    {
        if (error)
            *error = QStringLiteral("没有可写入 VTM 的 MATYPE 域。");
        return nullptr;
    }

    auto blocks = vtkSmartPointer<vtkMultiBlockDataSet>::New();
    blocks->SetNumberOfBlocks(static_cast<unsigned int>(elementsByMaterial.size()));
    unsigned int blockIndex = 0;
    const QSet<int> noMasks;
    for (auto it = elementsByMaterial.cbegin(); it != elementsByMaterial.cend(); ++it)
    {
        const int material = it.key();
        QString blockError;
        auto grid = buildVtuGrid(mesh, it.value(), noMasks, variables,
                                 pointValues, cellValues, &blockError, material);
        if (!grid)
        {
            if (error)
                *error = blockError;
            return nullptr;
        }

        grid = PostTecplotUtils::normalizeSolverTopology(grid);
        if (!grid || grid->GetNumberOfPoints() <= 0 || grid->GetNumberOfCells() <= 0)
        {
            if (error)
                *error = QStringLiteral("MATYPE=%1 的拓扑规范化结果为空。").arg(material);
            return nullptr;
        }
        markNormalizedTopology(grid);

        blocks->SetBlock(blockIndex, grid);
        const QByteArray blockName =
            QStringLiteral("MATYPE=%1").arg(material).toUtf8();
        blocks->GetMetaData(blockIndex)->Set(vtkCompositeDataSet::NAME(),
                                             blockName.constData());
        ++blockIndex;
    }
    return blocks;
}

bool writeVtmDataSet(const QString& filePath, vtkMultiBlockDataSet* blocks,
                     QString* error)
{
    if (!blocks || blocks->GetNumberOfBlocks() <= 0)
    {
        if (error)
            *error = QStringLiteral("后处理 VTM 没有 MATYPE Block：%1").arg(filePath);
        return false;
    }

    const QFileInfo targetInfo(filePath);
    const QString packageBase = targetInfo.completeBaseName();
    for (const QChar ch : packageBase)
    {
        if (ch.unicode() > 0x7f)
        {
            if (error)
                *error = QStringLiteral("VTM 文件名必须使用 ASCII 字符：%1")
                             .arg(targetInfo.fileName());
            return false;
        }
    }

    // VTM 是一个元文件加同名叶子目录。先让 VTK 在 ASCII 临时目录完整写出，
    // 再由 Qt 把叶子目录和元文件复制到中文工程路径；元文件最后落地，避免页面
    // 看到只写了一半的 MultiBlock 包。
    QTemporaryDir temporary(
        QDir(QDir::tempPath()).filePath(QStringLiteral("cae_post_vtm_XXXXXX")));
    if (!temporary.isValid())
    {
        if (error)
            *error = QStringLiteral("无法创建 VTM 临时目录：%1").arg(filePath);
        return false;
    }

    const QString temporaryRoot = temporary.filePath(targetInfo.fileName());
    auto writer = vtkSmartPointer<vtkXMLMultiBlockDataWriter>::New();
    writer->SetInputData(blocks);
    const QByteArray encodedPath = temporaryRoot.toLocal8Bit();
    writer->SetFileName(encodedPath.constData());
    writer->SetDataModeToAppended();
    writer->EncodeAppendedDataOff();
    writer->SetCompressorTypeToNone();
    if (writer->Write() != 1)
    {
        if (error)
            *error = QStringLiteral("写入后处理 VTM 失败：%1").arg(filePath);
        return false;
    }

    const QString temporaryLeafDir = temporary.filePath(packageBase);
    QDirIterator leafCheck(temporaryLeafDir, QStringList{QStringLiteral("*.vtu")}, QDir::Files,
                           QDirIterator::Subdirectories);
    if (!QFileInfo(temporaryRoot).isFile() || QFileInfo(temporaryRoot).size() <= 0 ||
        !QDir(temporaryLeafDir).exists() || !leafCheck.hasNext())
    {
        if (error)
            *error = QStringLiteral("VTK 未生成完整的 VTM 数据包：%1").arg(filePath);
        return false;
    }

    const QString targetLeafDir = targetInfo.dir().filePath(packageBase);
    QFile::remove(filePath);
    if (QDir(targetLeafDir).exists() && !QDir(targetLeafDir).removeRecursively())
    {
        if (error)
            *error = QStringLiteral("无法替换旧 VTM 数据目录：%1").arg(targetLeafDir);
        return false;
    }
    if (!copyDirectoryContents(temporaryLeafDir, targetLeafDir, error))
    {
        QDir(targetLeafDir).removeRecursively();
        return false;
    }
    if (!QFile::copy(temporaryRoot, filePath))
    {
        QDir(targetLeafDir).removeRecursively();
        if (error)
            *error = QStringLiteral("无法复制 VTM 元文件到工程目录：%1").arg(filePath);
        return false;
    }

    QFile::remove(PostTecplotUtils::surfaceCachePath(filePath));
    QFile::remove(PostTecplotUtils::domainSurfaceCachePath(filePath));
    return true;
}

bool writePointFieldVtm(const QString& filePath, const Mesh& mesh,
                        const QVector<Element>& expanded,
                        const QSet<int>& maskedMaterials,
                        const QStringList& variables,
                        const QVector<QVector<double>>& values,
                        QString* error)
{
    auto blocks = buildMaterialBlocks(mesh, expanded, maskedMaterials, variables,
                                      &values, nullptr, error);
    return blocks && writeVtmDataSet(filePath, blocks, error);
}

bool writeCellFieldVtm(const QString& filePath, const Mesh& mesh,
                       const QVector<Element>& expanded,
                       const QSet<int>& maskedMaterials,
                       const QStringList& variables,
                       const QHash<int, QVector<double>>& values,
                       QString* error)
{
    auto blocks = buildMaterialBlocks(mesh, expanded, maskedMaterials, variables,
                                      nullptr, &values, error);
    return blocks && writeVtmDataSet(filePath, blocks, error);
}

bool writePointFieldVtu(const QString& filePath, const Mesh& mesh,
                        const QVector<Element>& expanded,
                        const QSet<int>& maskedMaterials,
                        const QStringList& variables,
                        const QVector<QVector<double>>& values,
                        QString* error)
{
    auto grid = buildVtuGrid(mesh, expanded, maskedMaterials, variables,
                             &values, nullptr, error);
    if (!grid)
        return false;

    // Solver IIIN 的混合线性实体使用统一 8 节点槽位：Tet/Pyramid/Wedge
    // 可能以 KETYPE=6 的退化 BRICK 形式出现。不能把这种连接直接写进 VTU
    // 再指望显示阶段修复，否则大混合网格会在 Surface 提取前产生错误拓扑。
    // 在缓存生成边界就恢复为真实 VTK_TETRA/VTK_PYRAMID/VTK_WEDGE/HEX。
    grid = PostTecplotUtils::normalizeSolverTopology(grid);
    if (!grid || grid->GetNumberOfCells() <= 0)
    {
        if (error)
            *error = QStringLiteral("后处理网格拓扑规范化失败：%1").arg(filePath);
        return false;
    }
    return writeVtuGrid(filePath, grid, error);
}

bool writeCellFieldVtu(const QString& filePath, const Mesh& mesh,
                       const QVector<Element>& expanded,
                       const QSet<int>& maskedMaterials,
                       const QStringList& variables,
                       const QHash<int, QVector<double>>& values,
                       QString* error)
{
    auto grid = buildVtuGrid(mesh, expanded, maskedMaterials, variables,
                             nullptr, &values, error);
    if (!grid)
        return false;
    grid = PostTecplotUtils::normalizeSolverTopology(grid);
    if (!grid || grid->GetNumberOfCells() <= 0)
    {
        if (error)
            *error = QStringLiteral("后处理网格拓扑规范化失败：%1").arg(filePath);
        return false;
    }
    return writeVtuGrid(filePath, grid, error);
}

QSet<int> readMaterialSet(const QString& path)
{
    QSet<int> materials;
    if (path.isEmpty())
        return materials;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return materials;

    QTextStream in(&file);
    QString line;
    int count = 0;
    if (readNextNonEmptyLine(in, line))
        count = splitFields(line).value(0).toInt();
    for (int i = 0; i < count && readNextNonEmptyLine(in, line); ++i)
    {
        bool ok = false;
        const int material = splitFields(line).value(0).toInt(&ok);
        if (ok)
            materials.insert(material);
    }
    return materials;
}

QVector<ZoneGroup> loadZoneGroups(const QDir& resultDir, const QString& projectDir,
                                  const QVector<Element>& expanded)
{
    QSet<int> availableMaterials;
    for (const Element& element : expanded)
        availableMaterials.insert(element.material);

    QVector<ZoneGroup> groups;
    const QString indexPath = findInResultOrInput(resultDir, projectDir,
                                                  QStringLiteral("MAFILESET.DAT"));
    if (!indexPath.isEmpty())
    {
        QFile file(indexPath);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            QTextStream in(&file);
            QString line;
            int count = 0;
            if (readNextNonEmptyLine(in, line))
                count = splitFields(line).value(0).toInt();
            const QDir indexDir = QFileInfo(indexPath).dir();
            for (int i = 0; i < count && readNextNonEmptyLine(in, line); ++i)
            {
                const QVector<QString> fields = splitFields(line);
                if (fields.isEmpty())
                    continue;

                ZoneGroup group;
                group.name = fields.at(0);
                QString groupFileName = group.name;
                if (QFileInfo(groupFileName).suffix().isEmpty())
                    groupFileName += QStringLiteral(".DAT");
                QString groupPath = existingFile(indexDir, groupFileName);
                if (groupPath.isEmpty())
                    groupPath = findInResultOrInput(resultDir, projectDir, groupFileName);
                group.materials = readMaterialSet(groupPath);

                if (group.materials.isEmpty() && fields.size() >= 3)
                {
                    bool materialOk = false;
                    const int material = fields.at(2).toInt(&materialOk);
                    if (materialOk)
                        group.materials.insert(material);
                }
                group.materials.intersect(availableMaterials);
                if (!group.materials.isEmpty())
                    groups.push_back(group);
            }
        }
    }

    QSet<int> groupedMaterials;
    for (const ZoneGroup& group : groups)
        groupedMaterials.unite(group.materials);

    QList<int> materials;
    for (int material : availableMaterials)
        if (!groupedMaterials.contains(material))
            materials.push_back(material);
    std::sort(materials.begin(), materials.end());
    for (int material : materials)
    {
        ZoneGroup group;
        group.name = QStringLiteral("材料 %1").arg(material);
        group.materials.insert(material);
        groups.push_back(group);
    }
    return groups;
}

QVector<QVector<double>> readNodeValues(const QString& filePath, const Mesh& mesh, int minColumns,
                                        QString* warning)
{
    QVector<QVector<double>> values(mesh.nodes.size());
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        if (warning)
            *warning = QStringLiteral("无法打开节点结果：%1").arg(filePath);
        return values;
    }

    int rowsRead = 0;
    QTextStream in(&file);
    while (!in.atEnd())
    {
        const QVector<QString> fields = splitFields(in.readLine());
        if (fields.size() < minColumns + 1)
            continue;

        bool idOk = false;
        const int nodeId = fields.at(0).toInt(&idOk);
        const int nodeIndex = mesh.nodeIdToIndex.value(nodeId, -1);
        if (!idOk || nodeIndex < 0)
            continue;

        QVector<double> row;
        row.reserve(fields.size() - 1);
        bool rowOk = true;
        for (int i = 1; i < fields.size(); ++i)
        {
            bool valueOk = false;
            const double value = fields.at(i).toDouble(&valueOk);
            // 求解结果里偶尔会出现 NaN/Inf（或某些工况只输出部分节点）。
            // 内部后处理缓存不能把非有限值继续写给 VTK，否则 LUT 会按 NanColor
            // 显示成大片灰色。把该行视为缺失行，后续沿用既有 safeValue=0 的
            // 兼容规则，同时 rowsRead 不计入，最终 warnings 会明确提示数量不一致。
            if (!valueOk || !std::isfinite(value))
            {
                rowOk = false;
                break;
            }
            row.push_back(value);
        }
        if (rowOk && row.size() >= minColumns)
        {
            if (values[nodeIndex].isEmpty())
                ++rowsRead;
            values[nodeIndex] = row;
        }
    }

    if (warning && rowsRead != mesh.nodes.size())
        *warning = QStringLiteral("节点结果数量与网格不一致：%1，读取 %2/%3")
                       .arg(filePath)
                       .arg(rowsRead)
                       .arg(mesh.nodes.size());
    return values;
}

double safeValue(const QVector<double>& row, int index)
{
    if (index < 0 || index >= row.size())
        return 0.0;
    const double value = row.at(index);
    return std::isfinite(value) ? value : 0.0;
}

QVector<QVector<double>> buildHeadPressure(const QVector<QVector<double>>& raw)
{
    QVector<QVector<double>> values(raw.size());
    for (int i = 0; i < raw.size(); ++i)
        values[i] = {safeValue(raw.at(i), 0), safeValue(raw.at(i), 1) * 1.0e-6};
    return values;
}

QVector<QVector<double>> buildVelocity(const QVector<QVector<double>>& raw,
                                       const QVector<QVector<double>>& headPressure, int dimension)
{
    QVector<QVector<double>> values(raw.size());
    for (int i = 0; i < raw.size(); ++i)
    {
        const bool dryNode = i < headPressure.size() && headPressure.at(i).size() >= 2 &&
                             headPressure.at(i).at(1) < 0.0;
        const double scale = dryNode ? 0.0 : 1000.0;
        const double vx = safeValue(raw.at(i), 0) * scale;
        const double vy = safeValue(raw.at(i), 1) * scale;
        const double vz = dimension == 3 ? safeValue(raw.at(i), 2) * scale : 0.0;
        values[i] = {vx, vy};
        if (dimension == 3)
            values[i].push_back(vz);
        values[i].push_back(std::sqrt(vx * vx + vy * vy + vz * vz));
    }
    return values;
}

QVector<QVector<double>> buildDisplacement(const QVector<QVector<double>>& raw, int dimension)
{
    QVector<QVector<double>> values(raw.size());
    for (int i = 0; i < raw.size(); ++i)
    {
        const double ux = safeValue(raw.at(i), 0) * 100.0;
        const double uy = safeValue(raw.at(i), 1) * 100.0;
        const double uz = dimension == 3 ? safeValue(raw.at(i), 2) * 100.0 : 0.0;
        values[i] = {ux, uy};
        if (dimension == 3)
            values[i].push_back(uz);
        values[i].push_back(std::sqrt(ux * ux + uy * uy + uz * uz));
    }
    return values;
}

QVector<QVector<double>> buildPrincipalStress(const QVector<QVector<double>>& raw, int dimension)
{
    QVector<QVector<double>> values(raw.size());
    for (int i = 0; i < raw.size(); ++i)
    {
        if (dimension == 2)
        {
            const double p1 = -safeValue(raw.at(i), 1) * 1.0e-6;
            const double p2 = -safeValue(raw.at(i), 0) * 1.0e-6;
            values[i] = {p1, p2, std::abs(p1 - p2) * 0.5};
        }
        else
        {
            const double p1 = -safeValue(raw.at(i), 2) * 1.0e-6;
            const double p2 = -safeValue(raw.at(i), 1) * 1.0e-6;
            const double p3 = -safeValue(raw.at(i), 0) * 1.0e-6;
            values[i] = {p1, p2, p3, std::abs(p1 - p3) * 0.5};
        }
    }
    return values;
}

QVector<QVector<double>> buildStressLevel(const QVector<QVector<double>>& raw)
{
    QVector<QVector<double>> values(raw.size());
    for (int i = 0; i < raw.size(); ++i)
        values[i] = {safeValue(raw.at(i), 3)};
    return values;
}

QVector<QVector<double>> buildPlasticStrainComponents(const QVector<QVector<double>>& raw,
                                                       int dimension)
{
    constexpr double microstrain = -1.0e6;
    QVector<QVector<double>> values(raw.size());
    for (int i = 0; i < raw.size(); ++i)
    {
        if (dimension == 2)
        {
            values[i] = {safeValue(raw.at(i), 0) * microstrain,
                         safeValue(raw.at(i), 1) * microstrain,
                         safeValue(raw.at(i), 2) * microstrain};
        }
        else
        {
            values[i] = {safeValue(raw.at(i), 0) * microstrain,
                         safeValue(raw.at(i), 1) * microstrain,
                         safeValue(raw.at(i), 2) * microstrain,
                         safeValue(raw.at(i), 3) * microstrain,
                         safeValue(raw.at(i), 4) * microstrain,
                         safeValue(raw.at(i), 5) * microstrain};
        }
    }
    return values;
}

QVector<QVector<double>> buildPrincipalPlasticStrain(const QVector<QVector<double>>& raw,
                                                      int dimension)
{
    constexpr double microstrain = -1.0e6;
    QVector<QVector<double>> values(raw.size());
    for (int i = 0; i < raw.size(); ++i)
    {
        if (dimension == 2)
        {
            const double pe1 = safeValue(raw.at(i), 1) * microstrain;
            const double pe3 = safeValue(raw.at(i), 0) * microstrain;
            const double maximum = std::abs(pe1 - pe3) * 0.5;
            const double equivalent = std::abs(pe1 - pe3) / std::sqrt(2.0) * 2.0 / 3.0;
            values[i] = {pe1, pe3, maximum, equivalent};
        }
        else
        {
            const double pe1 = safeValue(raw.at(i), 2) * microstrain;
            const double pe2 = safeValue(raw.at(i), 1) * microstrain;
            const double pe3 = safeValue(raw.at(i), 0) * microstrain;
            const double maximum = std::abs(pe1 - pe3) * 0.5;
            const double equivalent = std::sqrt(
                0.5 * ((pe1 - pe2) * (pe1 - pe2) +
                       (pe2 - pe3) * (pe2 - pe3) +
                       (pe3 - pe1) * (pe3 - pe1))) * 2.0 / 3.0;
            values[i] = {pe1, pe2, pe3, maximum, equivalent};
        }
    }
    return values;
}

QHash<int, QVector<double>> readElementRuptureValues(const QString& filePath, const Mesh& mesh,
                                                      QString* warning)
{
    QHash<int, QVector<double>> values;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        if (warning)
            *warning = QStringLiteral("无法打开高斯点破坏结果：%1").arg(filePath);
        return values;
    }

    QSet<int> elementIds;
    for (const Element& element : mesh.elements)
        elementIds.insert(element.id);

    QHash<int, RuptureFlags> flagsByElement;
    int rowsRead = 0;
    QTextStream in(&file);
    while (!in.atEnd())
    {
        const QVector<QString> fields = splitFields(in.readLine());
        if (fields.size() < 9)
            continue;

        bool elementOk = false;
        bool gaussPointOk = false;
        bool ruptureOk = false;
        bool discriminatorOk = false;
        const int elementId = fields.at(0).toInt(&elementOk);
        const int gaussPoint = fields.at(1).toInt(&gaussPointOk);
        const double rupture = fields.at(7).toDouble(&ruptureOk);
        fields.at(8).toDouble(&discriminatorOk);
        if (!elementOk || !gaussPointOk || !ruptureOk || !discriminatorOk ||
            gaussPoint <= 0 || gaussPoint > 64 || !elementIds.contains(elementId))
            continue;

        ++rowsRead;
        const int ruptureCode = static_cast<int>(std::lround(rupture));
        RuptureFlags& flags = flagsByElement[elementId];
        if (ruptureCode == 2)
            flags.tensile = true;
        else if (ruptureCode != 0)
            flags.shear = true;
    }

    if (rowsRead == 0)
    {
        if (warning)
            *warning = QStringLiteral("高斯点破坏结果中没有可识别的数据：%1").arg(filePath);
        return values;
    }

    values.reserve(mesh.elements.size());
    for (const Element& element : mesh.elements)
    {
        const RuptureFlags flags = flagsByElement.value(element.id);
        const double failureType = flags.tensile ? 2.0 : (flags.shear ? 1.0 : 0.0);
        values.insert(element.id, {failureType});
    }
    return values;
}

QVector<FieldSource> fieldSources(const QDir& dir, const QString& prefix, bool seepage)
{
    QVector<FieldSource> sources;
    const QFileInfoList files = dir.entryInfoList(QDir::Files, QDir::Name | QDir::IgnoreCase);
    const QRegularExpression stepExpression(
        QStringLiteral("^STEP(\\d+)[_-](.+)$"),
        QRegularExpression::CaseInsensitiveOption);

    auto acceptedCondition = [seepage](const QString& condition)
    {
        if (condition.isEmpty())
            return true;
        if (!seepage)
            return condition.startsWith(QStringLiteral("CZ"), Qt::CaseInsensitive);
        return condition.startsWith(QStringLiteral("METHOD"), Qt::CaseInsensitive) ||
               condition.startsWith(QStringLiteral("CZ"), Qt::CaseInsensitive);
    };

    for (const QFileInfo& info : files)
    {
        if (info.suffix().compare(QStringLiteral("TXT"), Qt::CaseInsensitive) != 0)
            continue;

        QString base = info.completeBaseName();
        QString sequencePrefix;
        const QRegularExpressionMatch stepMatch = stepExpression.match(base);
        if (stepMatch.hasMatch())
        {
            sequencePrefix = QStringLiteral("STEP%1").arg(stepMatch.captured(1).toInt());
            base = stepMatch.captured(2);
        }

        if (!base.startsWith(prefix, Qt::CaseInsensitive))
            continue;

        QString condition = base.mid(prefix.size());
        if (!condition.isEmpty() && condition.front() != QLatin1Char('_') &&
            condition.front() != QLatin1Char('-'))
            continue;
        while (condition.startsWith(QLatin1Char('_')) ||
               condition.startsWith(QLatin1Char('-')))
            condition.remove(0, 1);
        if (!acceptedCondition(condition))
            continue;

        QString suffix = sequencePrefix;
        if (!condition.isEmpty())
        {
            if (!suffix.isEmpty())
                suffix += QLatin1Char('_');
            suffix += condition;
        }
        sources.push_back({info.absoluteFilePath(), suffix});
    }

    auto sequenceStep = [](const QString& suffix)
    {
        static const QRegularExpression expression(
            QStringLiteral("^STEP(\\d+)(?:_|$)"),
            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch match = expression.match(suffix);
        return match.hasMatch() ? match.captured(1).toInt() : -1;
    };

    // 求解器通常会把最后一个 STEP 文件再复制成无后缀文件。避免同一最终
    // 时刻在“当前状态”和时间序列中重复出现。
    const auto baseSource = std::find_if(
        sources.cbegin(), sources.cend(), [](const FieldSource& source)
        {
            return source.suffix.isEmpty();
        });
    if (baseSource != sources.cend())
    {
        int latestIndex = -1;
        int latestStep = -1;
        for (int i = 0; i < sources.size(); ++i)
        {
            const int step = sequenceStep(sources.at(i).suffix);
            if (step > latestStep &&
                !sources.at(i).suffix.contains(QLatin1Char('_')))
            {
                latestStep = step;
                latestIndex = i;
            }
        }
        if (latestIndex >= 0 &&
            filesHaveSameContents(baseSource->path, sources.at(latestIndex).path))
        {
            const QString basePath = baseSource->path;
            sources.erase(std::remove_if(
                sources.begin(), sources.end(), [&](const FieldSource& source)
                {
                    return source.suffix.isEmpty() && source.path == basePath;
                }), sources.end());
        }
        else
        {
            static const QRegularExpression unitCzCbResult(
                QStringLiteral("^CZ0*100CB0*100$"),
                QRegularExpression::CaseInsensitiveOption);
            const QString basePath = baseSource->path;
            sources.erase(std::remove_if(
                sources.begin(), sources.end(), [&](const FieldSource& source)
                {
                    return unitCzCbResult.match(source.suffix).hasMatch() &&
                           filesHaveSameContents(basePath, source.path);
                }), sources.end());
        }
    }

    std::sort(sources.begin(), sources.end(), [&](const FieldSource& left,
                                                   const FieldSource& right)
    {
        const int leftStep = sequenceStep(left.suffix);
        const int rightStep = sequenceStep(right.suffix);
        if ((leftStep >= 0) != (rightStep >= 0))
            return leftStep < 0;
        if (leftStep >= 0 && leftStep != rightStep)
            return leftStep < rightStep;

        const bool leftMethod2 =
            left.suffix.endsWith(QStringLiteral("METHOD2"), Qt::CaseInsensitive);
        const bool rightMethod2 =
            right.suffix.endsWith(QStringLiteral("METHOD2"), Qt::CaseInsensitive);
        if (leftMethod2 != rightMethod2)
            return leftMethod2;
        return left.suffix.compare(right.suffix, Qt::CaseInsensitive) < 0;
    });
    return sources;
}

QString outputSuffix(const QString& suffix)
{
    QString normalized = suffix.toLower();
    normalized.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("_"));
    normalized = normalized.trimmed();
    while (normalized.startsWith(QLatin1Char('_')))
        normalized.remove(0, 1);
    while (normalized.endsWith(QLatin1Char('_')))
        normalized.chop(1);
    return normalized.isEmpty() ? QString() : QStringLiteral("_") + normalized;
}

bool isJointType(int type)
{
    return type == 7 || type == 8 || type == 9 || type == 17;
}

bool writeTecplot(const QString& filePath, const Mesh& mesh, const QVector<Element>& expanded,
                  const QVector<ZoneGroup>& groups, const QSet<int>& maskedMaterials,
                  const QString& title, const QStringList& variables,
                  const QVector<QVector<double>>& values, QString* error)
{
    Q_UNUSED(groups);

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
    {
        if (error)
            *error = QStringLiteral("无法写入后处理缓存：%1").arg(filePath);
        return false;
    }

    QTextStream out(&file);
    out.setRealNumberNotation(QTextStream::ScientificNotation);
    out.setRealNumberPrecision(8);
    out << "TITLE=\"" << title << "\"\n";
    out << "VARIABLES=";
    const QStringList coordinates = mesh.dimension == 3 ? QStringList{"X", "Y", "Z"}
                                                         : QStringList{"X", "Y"};
    const QStringList cellMetadata = {QStringLiteral("KETYPE"), QStringLiteral("MATYPE"),
                                      QStringLiteral("LAYER"), QStringLiteral("elem_id")};
    const QStringList allVariables = coordinates + QStringList{QStringLiteral("node_id")} +
                                     variables + cellMetadata;
    for (int i = 0; i < allVariables.size(); ++i)
    {
        if (i > 0)
            out << ", ";
        out << "\"" << allVariables.at(i) << "\"";
    }
    out << '\n';

    // 旧实现按“材料分组 × 拓扑”拆 Zone。现在 MATYPE/LAYER/KETYPE 已经作为
    // CELLCENTERED 元数据写进结果文件，材料拆 Zone 不再承担筛选职责，反而会让
    // 相邻材料重复写节点，并让 vtkTecplotReader 产生大量块后再 Append。
    // 因此这里只按拓扑拆 Zone：显示/筛选信息不丢，生成和首次读取都明显更轻。
    int writtenZones = 0;
    // VTK 9.2 vtkTecplotReader 仅支持 BRICK/TRIANGLE/QUADRILATERAL/TETRAHEDRON/POINT，
    // 不支持 Tecplot 的 LINESEG。杆单元(type=1)写入同一 .tec 会让 Reader 在读取到
    // LINESEG Zone 后中止，后续表现为云图缺块、MATYPE/LAYER 筛选异常以及“unknown record type”。
    // 云图缓存只承载面/体结果；杆单元结果应由独立杆单元 Actor 展示。
    const QList<int> topologyOrder = {3, 31, 6, 35, 38};
    for (int topology : topologyOrder)
    {
        QVector<const Element*> selected;
        selected.reserve(expanded.size() / topologyOrder.size());
        for (const Element& element : expanded)
        {
            if (element.type == topology && !maskedMaterials.contains(element.material) &&
                !isJointType(element.type))
            {
                selected.push_back(&element);
            }
        }
        if (selected.isEmpty())
            continue;

        const int nodeCount = topologyNodeCount(topology);
        const QString tecplotType = topologyName(topology);
        if (nodeCount <= 0 || tecplotType.isEmpty())
            continue;

        QSet<int> usedNodeIds;
        usedNodeIds.reserve(selected.size() * nodeCount);
        for (const Element* element : selected)
        {
            for (int i = 0; i < nodeCount; ++i)
            {
                const int nodeId = element->nodes.value(i);
                if (mesh.nodeIdToIndex.contains(nodeId))
                    usedNodeIds.insert(nodeId);
            }
        }

        QVector<int> orderedNodeIds;
        orderedNodeIds.reserve(usedNodeIds.size());
        QHash<int, int> localNodeIds;
        localNodeIds.reserve(usedNodeIds.size());
        for (const Node& node : mesh.nodes)
        {
            if (usedNodeIds.contains(node.id))
            {
                localNodeIds.insert(node.id, orderedNodeIds.size() + 1);
                orderedNodeIds.push_back(node.id);
            }
        }
        if (orderedNodeIds.isEmpty())
            continue;

        const int firstCellVariable = mesh.dimension + 2 + variables.size();
        const int lastCellVariable = firstCellVariable + cellMetadata.size() - 1;
        out << "ZONE N=" << orderedNodeIds.size()
            << ",E=" << selected.size()
            << ",F=FEBLOCK,ET=" << tecplotType
            << ",VARLOCATION=([" << firstCellVariable << '-' << lastCellVariable
            << "]=CELLCENTERED)\n";

        // QTextStream 已配置 ScientificNotation/precision，直接流式写值，避免为每一个
        // 节点/单元创建临时 QString。大洞室结果通常是百万级数值，这里能省掉大量分配。
        for (int coordinate = 0; coordinate < mesh.dimension; ++coordinate)
        {
            for (int nodeId : orderedNodeIds)
            {
                const int globalIndex = mesh.nodeIdToIndex.value(nodeId, -1);
                out << mesh.nodes.at(globalIndex).xyz.at(coordinate) << '\n';
            }
        }

        for (int nodeId : orderedNodeIds)
            out << nodeId << '\n';

        for (int variable = 0; variable < variables.size(); ++variable)
        {
            for (int nodeId : orderedNodeIds)
            {
                const int globalIndex = mesh.nodeIdToIndex.value(nodeId, -1);
                out << safeValue(values.value(globalIndex), variable) << '\n';
            }
        }

        for (const Element* element : selected)
            out << (element->sourceType > 0 ? element->sourceType : element->type) << '\n';
        for (const Element* element : selected)
            out << element->material << '\n';
        for (const Element* element : selected)
            out << element->layer << '\n';
        for (const Element* element : selected)
            out << element->id << '\n';

        for (const Element* element : selected)
        {
            for (int i = 0; i < nodeCount; ++i)
            {
                int localId = localNodeIds.value(element->nodes.value(i));
                if (localId <= 0)
                    localId = 1;
                if (i > 0)
                    out << ' ';
                out << localId;
            }
            out << '\n';
        }
        ++writtenZones;
    }

    if (writtenZones == 0)
    {
        file.close();
        file.remove();
        if (error)
            *error = QStringLiteral("没有可显示的有效材料分区：%1").arg(filePath);
        return false;
    }
    return true;
}

bool writeCellCenteredTecplot(const QString& filePath, const Mesh& mesh,
                              const QVector<Element>& expanded,
                              const QVector<ZoneGroup>& groups,
                              const QSet<int>& maskedMaterials, const QString& title,
                              const QStringList& variables,
                              const QHash<int, QVector<double>>& values, QString* error)
{
    Q_UNUSED(groups);

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
    {
        if (error)
            *error = QStringLiteral("无法写入后处理缓存：%1").arg(filePath);
        return false;
    }

    QTextStream out(&file);
    out.setRealNumberNotation(QTextStream::ScientificNotation);
    out.setRealNumberPrecision(8);
    out << "TITLE=\"" << title << "\"\n";
    out << "VARIABLES=";
    const QStringList coordinates = mesh.dimension == 3 ? QStringList{"X", "Y", "Z"}
                                                         : QStringList{"X", "Y"};
    const QStringList cellMetadata = {QStringLiteral("KETYPE"), QStringLiteral("MATYPE"),
                                      QStringLiteral("LAYER"), QStringLiteral("elem_id")};
    const QStringList allVariables = coordinates + QStringList{QStringLiteral("node_id")} +
                                     variables + cellMetadata;
    for (int i = 0; i < allVariables.size(); ++i)
    {
        if (i > 0)
            out << ", ";
        out << "\"" << allVariables.at(i) << "\"";
    }
    out << '\n';

    int writtenZones = 0;
    // VTK 9.2 vtkTecplotReader 仅支持 BRICK/TRIANGLE/QUADRILATERAL/TETRAHEDRON/POINT，
    // 不支持 Tecplot 的 LINESEG。杆单元(type=1)写入同一 .tec 会让 Reader 在读取到
    // LINESEG Zone 后中止，后续表现为云图缺块、MATYPE/LAYER 筛选异常以及“unknown record type”。
    // 云图缓存只承载面/体结果；杆单元结果应由独立杆单元 Actor 展示。
    const QList<int> topologyOrder = {3, 31, 6, 35, 38};
    for (int topology : topologyOrder)
    {
        QVector<const Element*> selected;
        selected.reserve(expanded.size() / topologyOrder.size());
        for (const Element& element : expanded)
        {
            if (element.type == topology && !maskedMaterials.contains(element.material) &&
                !isJointType(element.type))
            {
                selected.push_back(&element);
            }
        }
        if (selected.isEmpty())
            continue;

        const int nodeCount = topologyNodeCount(topology);
        const QString tecplotType = topologyName(topology);
        if (nodeCount <= 0 || tecplotType.isEmpty())
            continue;

        QSet<int> usedNodeIds;
        usedNodeIds.reserve(selected.size() * nodeCount);
        for (const Element* element : selected)
        {
            for (int i = 0; i < nodeCount; ++i)
            {
                const int nodeId = element->nodes.value(i);
                if (mesh.nodeIdToIndex.contains(nodeId))
                    usedNodeIds.insert(nodeId);
            }
        }

        QVector<int> orderedNodeIds;
        orderedNodeIds.reserve(usedNodeIds.size());
        QHash<int, int> localNodeIds;
        localNodeIds.reserve(usedNodeIds.size());
        for (const Node& node : mesh.nodes)
        {
            if (usedNodeIds.contains(node.id))
            {
                localNodeIds.insert(node.id, orderedNodeIds.size() + 1);
                orderedNodeIds.push_back(node.id);
            }
        }
        if (orderedNodeIds.isEmpty())
            continue;

        const int firstCellVariable = mesh.dimension + 2;
        const int lastCellVariable = firstCellVariable + variables.size() +
                                     cellMetadata.size() - 1;
        out << "ZONE N=" << orderedNodeIds.size()
            << ",E=" << selected.size()
            << ",F=FEBLOCK,ET=" << tecplotType << ',';
        if (firstCellVariable == lastCellVariable)
            out << "VARLOCATION=([" << firstCellVariable << "]=CELLCENTERED)\n";
        else
            out << "VARLOCATION=([" << firstCellVariable << '-' << lastCellVariable
                << "]=CELLCENTERED)\n";

        for (int coordinate = 0; coordinate < mesh.dimension; ++coordinate)
        {
            for (int nodeId : orderedNodeIds)
            {
                const int globalIndex = mesh.nodeIdToIndex.value(nodeId, -1);
                out << mesh.nodes.at(globalIndex).xyz.at(coordinate) << '\n';
            }
        }

        for (int nodeId : orderedNodeIds)
            out << nodeId << '\n';

        for (int variable = 0; variable < variables.size(); ++variable)
        {
            for (const Element* element : selected)
                out << safeValue(values.value(element->id), variable) << '\n';
        }

        for (const Element* element : selected)
            out << (element->sourceType > 0 ? element->sourceType : element->type) << '\n';
        for (const Element* element : selected)
            out << element->material << '\n';
        for (const Element* element : selected)
            out << element->layer << '\n';
        for (const Element* element : selected)
            out << element->id << '\n';

        for (const Element* element : selected)
        {
            for (int i = 0; i < nodeCount; ++i)
            {
                int localId = localNodeIds.value(element->nodes.value(i));
                if (localId <= 0)
                    localId = 1;
                if (i > 0)
                    out << ' ';
                out << localId;
            }
            out << '\n';
        }
        ++writtenZones;
    }

    if (writtenZones == 0)
    {
        file.close();
        file.remove();
        if (error)
            *error = QStringLiteral("没有可显示的有效材料分区：%1").arg(filePath);
        return false;
    }
    return true;
}

void addGenerated(PostResultGenerator::Result& result, const QString& path)
{
    result.generatedFiles << QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
}

bool generateField(const QDir& outputDir, const Mesh& mesh, const QVector<Element>& expanded,
                   const QVector<ZoneGroup>& groups, const QSet<int>& masks,
                   const QString& outputName, const QString& title,
                   const QStringList& variables, const QVector<QVector<double>>& values,
                   PostResultGenerator::Result& result,
                   const QVector<Element>* expandedWithExcavated = nullptr)
{
    bool generated = false;
    if (!expanded.isEmpty())
    {
        QString error;
        const QString outputPath = outputDir.filePath(outputName);
        const QString format = QFileInfo(outputName).suffix().toLower();
        const bool ok =
            format == QStringLiteral("vtm")
                ? writePointFieldVtm(outputPath, mesh, expanded, masks,
                                     variables, values, &error)
                : (format == QStringLiteral("vtu")
                       ? writePointFieldVtu(outputPath, mesh, expanded, masks,
                                            variables, values, &error)
                       : writeTecplot(outputPath, mesh, expanded, groups, masks,
                                      title, variables, values, &error));
        if (ok)
        {
            addGenerated(result, outputPath);
            generated = true;
        }
        else
        {
            result.warnings << error;
        }
    }

    if (expandedWithExcavated && !expandedWithExcavated->isEmpty())
    {
        const QFileInfo outputInfo(outputName);
        const QString extension = outputInfo.suffix();
        const QString withExcavatedName = outputInfo.completeBaseName() +
            QStringLiteral("_with_excavated") +
            (extension.isEmpty() ? QString() : QStringLiteral(".") + extension);
        const QString withExcavatedPath = outputDir.filePath(withExcavatedName);
        QString withExcavatedError;
        const QString format = QFileInfo(withExcavatedName).suffix().toLower();
        const bool ok =
            format == QStringLiteral("vtm")
                ? writePointFieldVtm(withExcavatedPath, mesh, *expandedWithExcavated,
                                     masks, variables, values, &withExcavatedError)
                : (format == QStringLiteral("vtu")
                       ? writePointFieldVtu(withExcavatedPath, mesh,
                                            *expandedWithExcavated, masks,
                                            variables, values, &withExcavatedError)
                       : writeTecplot(
                             withExcavatedPath, mesh, *expandedWithExcavated,
                             groups, masks,
                             title + QStringLiteral(" - WITH EXCAVATED ELEMENTS"),
                             variables, values, &withExcavatedError));
        if (ok)
        {
            addGenerated(result, withExcavatedPath);
            generated = true;
        }
        else
        {
            result.warnings << withExcavatedError;
        }
    }
    return generated;
}

bool generateCellField(const QDir& outputDir, const Mesh& mesh,
                       const QVector<Element>& expanded, const QVector<ZoneGroup>& groups,
                       const QSet<int>& masks, const QString& outputName, const QString& title,
                       const QStringList& variables,
                       const QHash<int, QVector<double>>& values,
                       PostResultGenerator::Result& result,
                       const QVector<Element>* expandedWithExcavated = nullptr)
{
    if (values.isEmpty())
        return false;

    bool generated = false;
    if (!expanded.isEmpty())
    {
        QString error;
        const QString outputPath = outputDir.filePath(outputName);
        const QString format = QFileInfo(outputName).suffix().toLower();
        const bool ok =
            format == QStringLiteral("vtm")
                ? writeCellFieldVtm(outputPath, mesh, expanded, masks,
                                    variables, values, &error)
                : (format == QStringLiteral("vtu")
                       ? writeCellFieldVtu(outputPath, mesh, expanded, masks,
                                           variables, values, &error)
                       : writeCellCenteredTecplot(outputPath, mesh, expanded,
                                                  groups, masks, title, variables,
                                                  values, &error));
        if (ok)
        {
            addGenerated(result, outputPath);
            generated = true;
        }
        else
        {
            result.warnings << error;
        }
    }

    if (expandedWithExcavated && !expandedWithExcavated->isEmpty())
    {
        const QFileInfo outputInfo(outputName);
        const QString extension = outputInfo.suffix();
        const QString withExcavatedName = outputInfo.completeBaseName() +
            QStringLiteral("_with_excavated") +
            (extension.isEmpty() ? QString() : QStringLiteral(".") + extension);
        const QString withExcavatedPath = outputDir.filePath(withExcavatedName);
        QString withExcavatedError;
        const QString format = QFileInfo(withExcavatedName).suffix().toLower();
        const bool ok =
            format == QStringLiteral("vtm")
                ? writeCellFieldVtm(withExcavatedPath, mesh, *expandedWithExcavated,
                                    masks, variables, values, &withExcavatedError)
                : (format == QStringLiteral("vtu")
                       ? writeCellFieldVtu(withExcavatedPath, mesh,
                                           *expandedWithExcavated, masks,
                                           variables, values, &withExcavatedError)
                       : writeCellCenteredTecplot(
                             withExcavatedPath, mesh, *expandedWithExcavated,
                             groups, masks,
                             title + QStringLiteral(" - WITH EXCAVATED ELEMENTS"),
                             variables, values, &withExcavatedError));
        if (ok)
        {
            addGenerated(result, withExcavatedPath);
            generated = true;
        }
        else
        {
            result.warnings << withExcavatedError;
        }
    }
    return generated;
}

QVector<QVector<double>> buildPlasticZoneStressValues(
    const Mesh& mesh,
    const QVector<QVector<double>>& principalStress,
    const QHash<int, QVector<double>>& ruptureValues)
{
    QSet<int> plasticNodeIds;
    for (const Element& element : mesh.elements)
    {
        const QVector<double> rupture = ruptureValues.value(element.id);
        if (rupture.isEmpty() || rupture.first() <= 0.5)
            continue;
        for (int nodeId : element.nodes)
        {
            if (nodeId > 0)
                plasticNodeIds.insert(nodeId);
        }
    }

    QVector<QVector<double>> values;
    values.reserve(mesh.nodes.size());
    for (int i = 0; i < mesh.nodes.size(); ++i)
    {
        double value = 0.0;
        if (plasticNodeIds.contains(mesh.nodes.at(i).id) &&
            i < principalStress.size() && !principalStress.at(i).isEmpty())
        {
            // 求解器 MODPRI 的第 1 项就是最大主应力 PRS1。这里只在已经发生
            // 剪切/拉伸破坏的区域保留 PRS1；非破坏区置 0，但显示端仍保留完整几何。
            value = principalStress.at(i).at(0);
        }
        values.push_back({value});
    }

    return values;
}

QString outputName(bool external, const QString& legacyName, const QString& internalBase,
                   const QString& suffix)
{
    if (!external && suffix.isEmpty())
        return legacyName;

    // 自动准备结果按 MATYPE 直接写成原生 VTM Block。LAYER/KETYPE/拓扑仍作为
    // CellData 保留，切面、切块和 ParaView 都只消费已有 Block，不再重建工程域。
    return internalBase + outputSuffix(suffix) +
           (external ? QStringLiteral(".vtm") : QStringLiteral(".tec"));
}
} // namespace


bool PostResultGenerator::ensureDomainSurfaceCache(const QString& resultFilePath,
                                                   QString* error)
{
    const QFileInfo resultInfo(resultFilePath);
    if (!resultInfo.exists() || !resultInfo.isFile())
    {
        if (error)
            *error = QStringLiteral("后处理结果文件不存在：%1").arg(resultFilePath);
        return false;
    }

    // 保留旧 API 只为二进制/源码兼容；VTM 已把 MATYPE 写成权威 Block，
    // 不允许页面再从结果单元重建 domains.vtp。若旧版本留下旁路缓存则清除。
    const QString legacyPath = PostTecplotUtils::domainSurfaceCachePath(resultFilePath);
    if (!legacyPath.isEmpty())
        QFile::remove(legacyPath);
    if (error)
        error->clear();
    return true;
}

PostResultGenerator::Result PostResultGenerator::generate(const Options& options)
{
    Result result;
    const QDir resultDir(QDir::fromNativeSeparators(options.resultDir.trimmed()));
    if (!resultDir.exists())
    {
        result.error = QStringLiteral("计算结果不存在：%1").arg(options.resultDir);
        return result;
    }

    // 洞室求解生成的 IIIN.TXT 会把已开挖单元的负层号改回正值，并可能把
    // 开挖材料归并到围岩材料。地应力结果仍按完整岩体显示，但显示元数据必须
    // 来自原始计算输入，否则开挖体会在云图中与围岩融为一体。
    QString meshPath;
    const bool geoStressResult =
        QFileInfo(resultDir.absolutePath()).fileName().compare(
            QStringLiteral("GEOSTR"), Qt::CaseInsensitive) == 0;
    if (geoStressResult)
    {
        meshPath = findInOriginalInput(resultDir, options.projectDir,
                                       QStringLiteral("IIIN.DAT"));
    }
    else if (excavationVisibility(resultDir).enabled)
    {
        meshPath = findInResultOrInput(resultDir, options.projectDir,
                                       QStringLiteral("IIIN.DAT"));
    }
    if (meshPath.isEmpty())
        meshPath = existingFile(resultDir, QStringLiteral("IIIN.TXT"));
    if (meshPath.isEmpty())
        meshPath = existingFile(resultDir, QStringLiteral("IIIN.DAT"));
    if (meshPath.isEmpty())
    {
        result.error = QStringLiteral("计算结果中缺少网格数据：%1").arg(resultDir.absolutePath());
        return result;
    }

    Mesh mesh;
    QString error;
    if (!parseMesh(meshPath, mesh, &error))
    {
        result.error = error;
        return result;
    }

    const QVector<Element> expanded = expandElements(mesh, resultDir);
    const QVector<Element> expandedWithExcavated = expandElements(mesh, resultDir, true);
    const QVector<Element>* optionalExpandedWithExcavated =
        expandedWithExcavated.size() > expanded.size() ? &expandedWithExcavated : nullptr;
    const QVector<ZoneGroup> groups = loadZoneGroups(
        resultDir, options.projectDir,
        optionalExpandedWithExcavated ? expandedWithExcavated : expanded);
    const bool external = !options.outputDir.trimmed().isEmpty();
    if (expanded.isEmpty() && !optionalExpandedWithExcavated)
    {
        result.error = QStringLiteral("计算网格没有可显示的有效单元：%1").arg(resultDir.absolutePath());
        return result;
    }
    // 原生 VTM 域直接来自 IIIN 单元的 MATYPE，不依赖 MAFILESET/ZONE 分组。
    // 只有显式生成旧 Tecplot 时才需要 ZoneGroup。
    if (!external && groups.isEmpty())
    {
        result.error = QStringLiteral("旧 Tecplot 输出缺少材料分区定义：%1")
                           .arg(resultDir.absolutePath());
        return result;
    }

    QDir outputDir(external ? QDir::fromNativeSeparators(options.outputDir.trimmed())
                            : resultDir.absolutePath());
    if (!outputDir.exists() && !outputDir.mkpath(QStringLiteral(".")))
    {
        result.error = QStringLiteral("无法创建后处理缓存：%1").arg(outputDir.absolutePath());
        return result;
    }

    const QSet<int> commonMasks = readMaterialSet(findInResultOrInput(
        resultDir, options.projectDir, QStringLiteral("MA_MASK.DAT")));
    QSet<int> stressMasks = readMaterialSet(findInResultOrInput(
        resultDir, options.projectDir, QStringLiteral("MA_MASK_CMM.DAT")));
    if (stressMasks.isEmpty())
        stressMasks = commonMasks;
    const QSet<int> noMasks;

    const QVector<FieldSource> velocitySources = fieldSources(resultDir, QStringLiteral("INVELO"), true);
    const QVector<FieldSource> displacementSources = fieldSources(resultDir, QStringLiteral("IIDISP"), false);
    const QVector<FieldSource> seepageHeadSources = fieldSources(resultDir, QStringLiteral("IIDISP"), true);
    const bool seepage = !velocitySources.isEmpty() ||
                         std::any_of(seepageHeadSources.cbegin(), seepageHeadSources.cend(),
                                     [](const FieldSource& source)
                                     {
                                         return source.suffix.startsWith(QStringLiteral("METHOD"),
                                                                         Qt::CaseInsensitive);
                                     });

    QVector<QVector<double>> preferredHeadPressure;
    QHash<QString, QVector<QVector<double>>> headPressureBySuffix;
    if (seepage)
    {
        for (const FieldSource& source : seepageHeadSources)
        {
            QString warning;
            const auto raw = readNodeValues(source.path, mesh, 2, &warning);
            if (!warning.isEmpty())
                result.warnings << warning;
            headPressureBySuffix.insert(source.suffix.toCaseFolded(), raw);
            if (preferredHeadPressure.isEmpty() ||
                source.suffix.compare(QStringLiteral("METHOD2"), Qt::CaseInsensitive) == 0)
                preferredHeadPressure = raw;
            generateField(outputDir, mesh, expanded, groups, noMasks,
                          outputName(external, QStringLiteral("TEC_HP.DAT"),
                                     QStringLiteral("head_pressure"), source.suffix),
                           QStringLiteral("HP-SEEPAGE (MPa)"),
                           {QStringLiteral("HEAD(m)"), QStringLiteral("P(MPa)")},
                           buildHeadPressure(raw), result, optionalExpandedWithExcavated);
        }

        for (const FieldSource& source : velocitySources)
        {
            QString warning;
            const auto raw = readNodeValues(source.path, mesh, mesh.dimension, &warning);
            if (!warning.isEmpty())
                result.warnings << warning;
            const QStringList variables = mesh.dimension == 3
                                              ? QStringList{QStringLiteral("UX(mm/s)"),
                                                            QStringLiteral("UY(mm/s)"),
                                                            QStringLiteral("UZ(mm/s)"),
                                                            QStringLiteral("VMAG(mm/s)")}
                                              : QStringList{QStringLiteral("UX(mm/s)"),
                                                            QStringLiteral("UY(mm/s)"),
                                                            QStringLiteral("VMAG(mm/s)")};
            const QVector<QVector<double>> frameHeadPressure =
                headPressureBySuffix.value(source.suffix.toCaseFolded(),
                                           preferredHeadPressure);
            generateField(outputDir, mesh, expanded, groups, noMasks,
                          outputName(external, QStringLiteral("TEC_VELO.DAT"),
                                     QStringLiteral("velocity"), source.suffix),
                           QStringLiteral("VELOCITY"), variables,
                           buildVelocity(raw, frameHeadPressure, mesh.dimension), result,
                           optionalExpandedWithExcavated);
        }
    }
    else
    {
        for (const FieldSource& source : displacementSources)
        {
            QString warning;
            const auto raw = readNodeValues(source.path, mesh, mesh.dimension, &warning);
            if (!warning.isEmpty())
                result.warnings << warning;
            const QStringList variables = mesh.dimension == 3
                                              ? QStringList{QStringLiteral("UX(cm)"),
                                                            QStringLiteral("UY(cm)"),
                                                            QStringLiteral("UZ(cm)"),
                                                            QStringLiteral("UH(cm)")}
                                              : QStringList{QStringLiteral("UX(cm)"),
                                                            QStringLiteral("UY(cm)"),
                                                            QStringLiteral("UH(cm)")};
            generateField(outputDir, mesh, expanded, groups, commonMasks,
                          outputName(external, QStringLiteral("TEC_DISP.DAT"),
                                     QStringLiteral("displacement"), source.suffix),
                           QStringLiteral("DISP (cm)"), variables,
                           buildDisplacement(raw, mesh.dimension), result,
                           optionalExpandedWithExcavated);
        }
    }

    const QVector<FieldSource> principalSources = fieldSources(resultDir, QStringLiteral("MODPRI"), false);
    for (const FieldSource& source : principalSources)
    {
        QString warning;
        const auto raw = readNodeValues(source.path, mesh, mesh.dimension == 3 ? 3 : 2, &warning);
        if (!warning.isEmpty())
            result.warnings << warning;
        const QStringList variables = mesh.dimension == 3
                                          ? QStringList{QStringLiteral("PRS1(MPa)"),
                                                        QStringLiteral("PRS2(MPa)"),
                                                        QStringLiteral("PRS3(MPa)"),
                                                        QStringLiteral("TMAX(MPa)")}
                                          : QStringList{QStringLiteral("PRS1(MPa)"),
                                                        QStringLiteral("PRS2(MPa)"),
                                                        QStringLiteral("TMAX(MPa)")};
        generateField(outputDir, mesh, expanded, groups, commonMasks,
                      outputName(external, QStringLiteral("TEC_PRS.DAT"),
                                 QStringLiteral("principal_stress"), source.suffix),
                       QStringLiteral("PRS (MPa)"), variables,
                       buildPrincipalStress(raw, mesh.dimension), result,
                       optionalExpandedWithExcavated);
    }

    const QVector<FieldSource> plasticComponentSources =
        fieldSources(resultDir, QStringLiteral("MODRAI_PLS"), false);
    for (const FieldSource& source : plasticComponentSources)
    {
        QString warning;
        const int componentCount = mesh.dimension == 3 ? 6 : 3;
        const auto raw = readNodeValues(source.path, mesh, componentCount, &warning);
        if (!warning.isEmpty())
            result.warnings << warning;
        const QStringList variables = mesh.dimension == 3
            ? QStringList{QStringLiteral("PEXX(1.0e-6eps)"),
                          QStringLiteral("PEYY(1.0e-6eps)"),
                          QStringLiteral("PEZZ(1.0e-6eps)"),
                          QStringLiteral("PEXY(1.0e-6eps)"),
                          QStringLiteral("PEYZ(1.0e-6eps)"),
                          QStringLiteral("PEZX(1.0e-6eps)")}
            : QStringList{QStringLiteral("PEXX(1.0e-6eps)"),
                          QStringLiteral("PEYY(1.0e-6eps)"),
                          QStringLiteral("PEXY(1.0e-6eps)")};
        generateField(outputDir, mesh, expanded, groups, commonMasks,
                      outputName(external, QStringLiteral("TEC_RAI_PLS.DAT"),
                                 QStringLiteral("plastic_strain"), source.suffix),
                      QStringLiteral("PLASTIC STRAIN COMPONENTS"), variables,
                      buildPlasticStrainComponents(raw, mesh.dimension), result,
                      optionalExpandedWithExcavated);
    }

    const QVector<FieldSource> principalPlasticSources =
        fieldSources(resultDir, QStringLiteral("MODRAP_PLS"), false);
    for (const FieldSource& source : principalPlasticSources)
    {
        QString warning;
        const auto raw = readNodeValues(source.path, mesh, mesh.dimension == 3 ? 3 : 2, &warning);
        if (!warning.isEmpty())
            result.warnings << warning;
        const QStringList variables = mesh.dimension == 3
            ? QStringList{QStringLiteral("PE1(1.0e-6eps)"),
                          QStringLiteral("PE2(1.0e-6eps)"),
                          QStringLiteral("PE3(1.0e-6eps)"),
                          QStringLiteral("PEMAX(1.0e-6eps)"),
                          QStringLiteral("PEEQ(1.0e-6eps)")}
            : QStringList{QStringLiteral("PE1(1.0e-6eps)"),
                          QStringLiteral("PE3(1.0e-6eps)"),
                          QStringLiteral("PEMAX(1.0e-6eps)"),
                          QStringLiteral("PEEQ(1.0e-6eps)")};
        generateField(outputDir, mesh, expanded, groups, commonMasks,
                      outputName(external, QStringLiteral("TEC_RAP_PLS.DAT"),
                                 QStringLiteral("principal_plastic_strain"), source.suffix),
                      QStringLiteral("PRINCIPAL PLASTIC STRAIN"), variables,
                      buildPrincipalPlasticStrain(raw, mesh.dimension), result,
                      optionalExpandedWithExcavated);
    }

    const QVector<FieldSource> stressLevelSources = fieldSources(resultDir, QStringLiteral("NODCMM"), false);
    for (const FieldSource& source : stressLevelSources)
    {
        QString warning;
        const auto raw = readNodeValues(source.path, mesh, 4, &warning);
        if (!warning.isEmpty())
            result.warnings << warning;
        generateField(outputDir, mesh, expanded, groups, stressMasks,
                      outputName(external, QStringLiteral("TEC_SL.DAT"),
                                 QStringLiteral("stress_level"), source.suffix),
                       QStringLiteral("STRESS LEVEL"), {QStringLiteral("SL")},
                       buildStressLevel(raw), result, optionalExpandedWithExcavated);

        // NODCMM: PGEN/EG_UE, QGEN/EG_UT, GAMAMAX/EG_WP, SL, FUN, ...
        // Preserve the solver's signed FUN values and native scale. Its physical
        // meaning depends on the constitutive model; it is not a global FOS.
        // Older four-column files still provide SL, but cannot provide FUN.
        if (std::any_of(raw.cbegin(), raw.cend(),
                        [](const QVector<double>& row) { return row.size() >= 5; }))
        {
            QVector<QVector<double>> functionValues(raw.size());
            for (int i = 0; i < raw.size(); ++i)
                functionValues[i] = {safeValue(raw.at(i), 4)};
            generateField(outputDir, mesh, expanded, groups, stressMasks,
                          outputName(external, QStringLiteral("TEC_FUN.DAT"),
                                     QStringLiteral("material_function"), source.suffix),
                          QStringLiteral("MATERIAL FUNCTION FUN"), {QStringLiteral("FUN")},
                          functionValues, result, optionalExpandedWithExcavated);
        }
    }

    const QVector<FieldSource> ruptureSources = fieldSources(resultDir, QStringLiteral("IIICMM"), false);
    for (const FieldSource& source : ruptureSources)
    {
        QString warning;
        const auto values = readElementRuptureValues(source.path, mesh, &warning);
        if (!warning.isEmpty())
            result.warnings << warning;
        generateCellField(outputDir, mesh, expanded, groups, stressMasks,
                          outputName(external, QStringLiteral("TEC_RUPT.DAT"),
                                     QStringLiteral("failure_zone"), source.suffix),
                          QStringLiteral("PLASTIC FAILURE TYPE"),
                          {QStringLiteral("FAILURE_ZONE")},
                          values, result, optionalExpandedWithExcavated);
    }

    // 每个有强度折减输出的工况都补充“最大塑性应力”。这里不把塑性应变
    // 冒充应力，而是用 IIICMM 的实际破坏区筛出塑性/破坏区域，再读取同工况
    // MODPRI 的最大主应力 PRS1。非破坏区写 0 但保留完整几何，不再裁掉模型。
    QHash<QString, FieldSource> ruptureBySuffix;
    for (const FieldSource& source : ruptureSources)
        ruptureBySuffix.insert(source.suffix.toCaseFolded(), source);
    for (const FieldSource& stressSource : principalSources)
    {
        const auto ruptureIt = ruptureBySuffix.constFind(stressSource.suffix.toCaseFolded());
        if (ruptureIt == ruptureBySuffix.constEnd())
            continue;

        QString stressWarning;
        QString ruptureWarning;
        const auto rawStress = readNodeValues(stressSource.path, mesh,
                                              mesh.dimension == 3 ? 3 : 2,
                                              &stressWarning);
        const auto rupture = readElementRuptureValues(ruptureIt->path, mesh,
                                                      &ruptureWarning);
        if (!stressWarning.isEmpty())
            result.warnings << stressWarning;
        if (!ruptureWarning.isEmpty())
            result.warnings << ruptureWarning;
        if (rawStress.isEmpty() || rupture.isEmpty())
            continue;

        const auto principalStress = buildPrincipalStress(rawStress, mesh.dimension);
        const auto plasticZoneStress = buildPlasticZoneStressValues(
            mesh, principalStress, rupture);
        generateField(outputDir, mesh, expanded, groups, stressMasks,
                      outputName(external, QStringLiteral("TEC_PLS_STRESS.DAT"),
                                 QStringLiteral("plastic_zone_stress"), stressSource.suffix),
                      QStringLiteral("MAX PLASTIC STRESS"),
                      {QStringLiteral("PLASTIC_ZONE_PRS1(MPa)")},
                      plasticZoneStress, result, optionalExpandedWithExcavated);
    }


    result.ok = !result.generatedFiles.isEmpty();
    if (!result.ok)
        result.error = QStringLiteral("没有找到可转换的节点结果。");
    return result;
}
