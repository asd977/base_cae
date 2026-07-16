#include "PostResultGenerator.h"

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
    int type = 0;
    int material = 1;
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

QString findInResultOrPrep(const QDir& resultDir, const QString& projectDir, const QString& name)
{
    QString path = existingFile(resultDir, name);
    if (!path.isEmpty())
        return path;

    if (!projectDir.trimmed().isEmpty())
        path = existingFile(QDir(QDir(projectDir).filePath(QStringLiteral("PREP"))), name);
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
        element.material = fields.at(materialIndex).toInt();
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

QVector<Element> expandElements(const Mesh& mesh)
{
    QVector<Element> expanded;
    expanded.reserve(mesh.elements.size());
    for (const Element& element : mesh.elements)
    {
        if (element.material != 1)
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
    QVector<ZoneGroup> groups;
    const QString indexPath = findInResultOrPrep(resultDir, projectDir,
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
                    groupPath = findInResultOrPrep(resultDir, projectDir, groupFileName);
                group.materials = readMaterialSet(groupPath);

                if (group.materials.isEmpty() && fields.size() >= 3)
                {
                    bool materialOk = false;
                    const int material = fields.at(2).toInt(&materialOk);
                    if (materialOk)
                        group.materials.insert(material);
                }
                if (!group.materials.isEmpty())
                    groups.push_back(group);
            }
        }
    }

    if (!groups.isEmpty())
        return groups;

    QList<int> materials;
    QSet<int> seen;
    for (const Element& element : expanded)
    {
        if (!seen.contains(element.material))
        {
            seen.insert(element.material);
            materials.push_back(element.material);
        }
    }
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
        values[i] = {safeValue(raw.at(i), 0) * scale, safeValue(raw.at(i), 1) * scale};
        if (dimension == 3)
            values[i].push_back(safeValue(raw.at(i), 2) * scale);
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

QVector<FieldSource> fieldSources(const QDir& dir, const QString& prefix, bool seepage)
{
    QVector<FieldSource> sources;
    const QFileInfoList files = dir.entryInfoList(QDir::Files, QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo& info : files)
    {
        const QString base = info.completeBaseName();
        if (!base.startsWith(prefix, Qt::CaseInsensitive) ||
            info.suffix().compare(QStringLiteral("TXT"), Qt::CaseInsensitive) != 0)
            continue;

        QString suffix = base.mid(prefix.size());
        if (!suffix.isEmpty() && suffix.front() != QLatin1Char('_') &&
            suffix.front() != QLatin1Char('-'))
            continue;
        while (suffix.startsWith(QLatin1Char('_')) || suffix.startsWith(QLatin1Char('-')))
            suffix.remove(0, 1);

        if (!seepage && !suffix.isEmpty() &&
            !suffix.startsWith(QStringLiteral("CZ"), Qt::CaseInsensitive))
            continue;
        if (seepage && !suffix.isEmpty() &&
            !suffix.startsWith(QStringLiteral("METHOD"), Qt::CaseInsensitive) &&
            !suffix.startsWith(QStringLiteral("CZ"), Qt::CaseInsensitive))
            continue;

        sources.push_back({info.absoluteFilePath(), suffix});
    }

    std::sort(sources.begin(), sources.end(), [](const FieldSource& left, const FieldSource& right)
    {
        if (left.suffix.isEmpty() != right.suffix.isEmpty())
            return left.suffix.isEmpty();
        const bool leftMethod2 = left.suffix.compare(QStringLiteral("METHOD2"), Qt::CaseInsensitive) == 0;
        const bool rightMethod2 = right.suffix.compare(QStringLiteral("METHOD2"), Qt::CaseInsensitive) == 0;
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
    const QStringList allVariables = coordinates + variables;
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

            out << QString("ZONE N=%1,E=%2 ,F=FEPOINT,ET=%3\n")
                       .arg(orderedNodeIds.size(), 8)
                       .arg(selected.size(), 8)
                       .arg(tecplotType);
            for (int nodeId : orderedNodeIds)
            {
                const int globalIndex = mesh.nodeIdToIndex.value(nodeId, -1);
                const Node& node = mesh.nodes.at(globalIndex);
                out << QString("%1 %2")
                           .arg(node.xyz.at(0), 16, 'E', 8)
                           .arg(node.xyz.at(1), 16, 'E', 8);
                if (mesh.dimension == 3)
                    out << QString(" %1").arg(node.xyz.at(2), 16, 'E', 8);
                const QVector<double>& row = values.value(globalIndex);
                for (int i = 0; i < variables.size(); ++i)
                    out << QString(" %1").arg(safeValue(row, i), 16, 'E', 8);
                out << "\n";
            }

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
                   PostResultGenerator::Result& result)
{
    QString error;
    const QString outputPath = outputDir.filePath(outputName);
    if (!writeTecplot(outputPath, mesh, expanded, groups, masks, title, variables, values, &error))
    {
        result.warnings << error;
        return false;
    }
    addGenerated(result, outputPath);
    return true;
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

    QString meshPath = existingFile(resultDir, QStringLiteral("IIIN.TXT"));
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

    const QVector<Element> expanded = expandElements(mesh);
    const QVector<ZoneGroup> groups = loadZoneGroups(resultDir, options.projectDir, expanded);
    if (expanded.isEmpty() || groups.isEmpty())
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

    const QSet<int> commonMasks = readMaterialSet(findInResultOrPrep(
        resultDir, options.projectDir, QStringLiteral("MA_MASK.DAT")));
    QSet<int> stressMasks = readMaterialSet(findInResultOrPrep(
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
    if (seepage)
    {
        for (const FieldSource& source : seepageHeadSources)
        {
            QString warning;
            const auto raw = readNodeValues(source.path, mesh, 2, &warning);
            if (!warning.isEmpty())
                result.warnings << warning;
            if (preferredHeadPressure.isEmpty() ||
                source.suffix.compare(QStringLiteral("METHOD2"), Qt::CaseInsensitive) == 0)
                preferredHeadPressure = raw;
            generateField(outputDir, mesh, expanded, groups, noMasks,
                          outputName(external, QStringLiteral("TEC_HP.DAT"),
                                     QStringLiteral("head_pressure"), source.suffix),
                          QStringLiteral("HP-SEEPAGE (MPa)"),
                          {QStringLiteral("HEAD(m)"), QStringLiteral("P(MPa)")},
                          buildHeadPressure(raw), result);
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
                                                            QStringLiteral("UZ(mm/s)")}
                                              : QStringList{QStringLiteral("UX(mm/s)"),
                                                            QStringLiteral("UY(mm/s)")};
            generateField(outputDir, mesh, expanded, groups, noMasks,
                          outputName(external, QStringLiteral("TEC_VELO.DAT"),
                                     QStringLiteral("velocity"), source.suffix),
                          QStringLiteral("VELOCITY"), variables,
                          buildVelocity(raw, preferredHeadPressure, mesh.dimension), result);
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
                          buildDisplacement(raw, mesh.dimension), result);
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
                      buildPrincipalStress(raw, mesh.dimension), result);
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
                      buildStressLevel(raw), result);
    }

    result.ok = !result.generatedFiles.isEmpty();
    if (!result.ok)
        result.error = QStringLiteral("没有找到可转换的节点结果。");
    return result;
}
