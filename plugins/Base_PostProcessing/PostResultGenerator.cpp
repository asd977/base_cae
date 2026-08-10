#include "PostResultGenerator.h"

#include <PublicConstants.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QTextStream>

#include <algorithm>
#include <cmath>

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
            if (!valueOk)
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
    return index >= 0 && index < row.size() ? row.at(index) : 0.0;
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
        values.insert(element.id,
                      {(flags.shear || flags.tensile) ? 1.0 : 0.0,
                       flags.shear ? 1.0 : 0.0,
                       flags.tensile ? 1.0 : 0.0});
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
    out << "\n";

    int writtenZones = 0;
    const QList<int> topologyOrder = {3, 31, 6, 35, 1, 38};
    for (const ZoneGroup& group : groups)
    {
        for (int topology : topologyOrder)
        {
            QVector<const Element*> selected;
            for (const Element& element : expanded)
            {
                if (element.type == topology && group.materials.contains(element.material) &&
                    !maskedMaterials.contains(element.material) && !isJointType(element.type))
                    selected.push_back(&element);
            }
            if (selected.isEmpty())
                continue;

            const int nodeCount = topologyNodeCount(topology);
            const QString tecplotType = topologyName(topology);
            if (nodeCount <= 0 || tecplotType.isEmpty())
                continue;

            QSet<int> usedNodeIds;
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

            // 使用 FEBLOCK 同时保存节点结果和单元分组元数据。这样后处理筛选
            // 不再依赖几何坐标，即便以后显示变形后的节点坐标也能稳定关联。
            const int firstCellVariable = mesh.dimension + 2 + variables.size();
            const int lastCellVariable = firstCellVariable + cellMetadata.size() - 1;
            out << QString("ZONE N=%1,E=%2,F=FEBLOCK,ET=%3,")
                       .arg(orderedNodeIds.size(), 8)
                       .arg(selected.size(), 8)
                       .arg(tecplotType)
                << QString("VARLOCATION=([%1-%2]=CELLCENTERED)\n")
                       .arg(firstCellVariable)
                       .arg(lastCellVariable);

            for (int coordinate = 0; coordinate < mesh.dimension; ++coordinate)
            {
                for (int nodeId : orderedNodeIds)
                {
                    const int globalIndex = mesh.nodeIdToIndex.value(nodeId, -1);
                    out << QString("%1\n").arg(mesh.nodes.at(globalIndex).xyz.at(coordinate),
                                               16, 'E', 8);
                }
            }

            for (int nodeId : orderedNodeIds)
                out << QString("%1\n").arg(nodeId);

            for (int variable = 0; variable < variables.size(); ++variable)
            {
                for (int nodeId : orderedNodeIds)
                {
                    const int globalIndex = mesh.nodeIdToIndex.value(nodeId, -1);
                    out << QString("%1\n").arg(safeValue(values.value(globalIndex), variable),
                                               16, 'E', 8);
                }
            }

            for (const Element* element : selected)
                out << QString("%1\n").arg(element->sourceType > 0 ? element->sourceType
                                                                       : element->type);
            for (const Element* element : selected)
                out << QString("%1\n").arg(element->material);
            for (const Element* element : selected)
                out << QString("%1\n").arg(element->layer);
            for (const Element* element : selected)
                out << QString("%1\n").arg(element->id);

            for (const Element* element : selected)
            {
                for (int i = 0; i < nodeCount; ++i)
                {
                    int localId = localNodeIds.value(element->nodes.value(i));
                    if (localId <= 0)
                        localId = 1;
                    out << QString("%1").arg(localId, 8);
                }
                out << "\n";
            }
            ++writtenZones;
        }
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
    out << "\n";

    int writtenZones = 0;
    const QList<int> topologyOrder = {3, 31, 6, 35, 1, 38};
    for (const ZoneGroup& group : groups)
    {
        for (int topology : topologyOrder)
        {
            QVector<const Element*> selected;
            for (const Element& element : expanded)
            {
                if (element.type == topology && group.materials.contains(element.material) &&
                    !maskedMaterials.contains(element.material) && !isJointType(element.type))
                    selected.push_back(&element);
            }
            if (selected.isEmpty())
                continue;

            const int nodeCount = topologyNodeCount(topology);
            const QString tecplotType = topologyName(topology);
            if (nodeCount <= 0 || tecplotType.isEmpty())
                continue;

            QSet<int> usedNodeIds;
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
            out << QString("ZONE N=%1,E=%2,F=FEBLOCK,ET=%3,")
                       .arg(orderedNodeIds.size(), 8)
                       .arg(selected.size(), 8)
                       .arg(tecplotType);
            if (firstCellVariable == lastCellVariable)
                out << QString("VARLOCATION=([%1]=CELLCENTERED)\n").arg(firstCellVariable);
            else
                out << QString("VARLOCATION=([%1-%2]=CELLCENTERED)\n")
                           .arg(firstCellVariable)
                           .arg(lastCellVariable);

            for (int coordinate = 0; coordinate < mesh.dimension; ++coordinate)
            {
                for (int nodeId : orderedNodeIds)
                {
                    const int globalIndex = mesh.nodeIdToIndex.value(nodeId, -1);
                    out << QString("%1\n").arg(mesh.nodes.at(globalIndex).xyz.at(coordinate),
                                               16, 'E', 8);
                }
            }
            for (int nodeId : orderedNodeIds)
                out << QString("%1\n").arg(nodeId);

            for (int variable = 0; variable < variables.size(); ++variable)
            {
                for (const Element* element : selected)
                    out << QString("%1\n").arg(safeValue(values.value(element->id), variable),
                                               16, 'E', 8);
            }

            for (const Element* element : selected)
                out << QString("%1\n").arg(element->sourceType > 0 ? element->sourceType
                                                                       : element->type);
            for (const Element* element : selected)
                out << QString("%1\n").arg(element->material);
            for (const Element* element : selected)
                out << QString("%1\n").arg(element->layer);
            for (const Element* element : selected)
                out << QString("%1\n").arg(element->id);

            for (const Element* element : selected)
            {
                for (int i = 0; i < nodeCount; ++i)
                {
                    int localId = localNodeIds.value(element->nodes.value(i));
                    if (localId <= 0)
                        localId = 1;
                    out << QString("%1").arg(localId, 8);
                }
                out << "\n";
            }
            ++writtenZones;
        }
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
        if (writeTecplot(outputPath, mesh, expanded, groups, masks, title, variables, values,
                         &error))
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
        if (writeTecplot(withExcavatedPath, mesh, *expandedWithExcavated, groups, masks,
                         title + QStringLiteral(" - WITH EXCAVATED ELEMENTS"),
                         variables, values, &withExcavatedError))
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
        if (writeCellCenteredTecplot(outputPath, mesh, expanded, groups, masks, title, variables,
                                     values, &error))
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
        if (writeCellCenteredTecplot(withExcavatedPath, mesh, *expandedWithExcavated, groups,
                                     masks, title + QStringLiteral(" - WITH EXCAVATED ELEMENTS"),
                                     variables, values, &withExcavatedError))
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

QString outputName(bool external, const QString& legacyName, const QString& internalBase,
                   const QString& suffix)
{
    if (!external && suffix.isEmpty())
        return legacyName;
    return internalBase + outputSuffix(suffix) + QStringLiteral(".tec");
}
} // namespace

PostResultGenerator::Result PostResultGenerator::generate(const Options& options)
{
    Result result;
    const QDir resultDir(QDir::fromNativeSeparators(options.resultDir.trimmed()));
    if (!resultDir.exists())
    {
        result.error = QStringLiteral("计算结果不存在：%1").arg(options.resultDir);
        return result;
    }

    // 洞室求解生成的 IIIN.TXT 会把已开挖单元的负层号改回正值；
    // 结果目录（或对应分析输入目录）的 IIIN.DAT 才保留施工阶段标记，
    // 阶段结果必须优先读取它。
    QString meshPath;
    if (excavationVisibility(resultDir).enabled)
        meshPath = findInResultOrInput(resultDir, options.projectDir,
                                       QStringLiteral("IIIN.DAT"));
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
    if ((expanded.isEmpty() && !optionalExpandedWithExcavated) || groups.isEmpty())
    {
        result.error = QStringLiteral("计算网格没有可显示的材料分区：%1").arg(resultDir.absolutePath());
        return result;
    }

    const bool external = !options.outputDir.trimmed().isEmpty();
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
                          QStringLiteral("FAILURE ZONE"),
                          {QStringLiteral("FAILURE_ZONE"),
                           QStringLiteral("SHEAR_FAILURE"),
                           QStringLiteral("TENSILE_FAILURE")},
                          values, result, optionalExpandedWithExcavated);
    }

    result.ok = !result.generatedFiles.isEmpty();
    if (!result.ok)
        result.error = QStringLiteral("没有找到可转换的节点结果。");
    return result;
}
