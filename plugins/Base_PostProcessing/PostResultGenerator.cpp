#include "PostResultGenerator.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QTextStream>

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
    int material = 1;
};

struct Mesh
{
    int dimension = 0;
    int nodeCount = 0;
    int elementCount = 0;
    int nodesPerElement = 0;
    QVector<Node> nodes;
    QVector<Element> elements;
    QHash<int, int> nodeIdToOutputIndex;
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

bool parseMesh(const QString& filePath, Mesh& mesh, QString* error)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        if (error)
            *error = QStringLiteral("无法打开网格文件：%1").arg(filePath);
        return false;
    }

    QTextStream in(&file);
    QString line;
    if (!readNextNonEmptyLine(in, line))
    {
        if (error)
            *error = QStringLiteral("网格文件为空：%1").arg(filePath);
        return false;
    }

    QVector<QString> header = splitFields(line);
    if (header.size() < 5)
    {
        if (error)
            *error = QStringLiteral("IIIN.DAT 首行格式不完整：%1").arg(filePath);
        return false;
    }

    bool ok = false;
    mesh.nodeCount = header.at(0).toInt(&ok);
    if (!ok || mesh.nodeCount <= 0)
    {
        if (error)
            *error = QStringLiteral("IIIN.DAT 节点数无效：%1").arg(filePath);
        return false;
    }
    mesh.dimension = header.at(1).toInt(&ok);
    mesh.elementCount = header.at(2).toInt(&ok);
    mesh.nodesPerElement = header.at(3).toInt(&ok);

    if (mesh.dimension != 2 && mesh.dimension != 3)
    {
        if (error)
            *error = QStringLiteral("暂不支持的网格维度：%1").arg(mesh.dimension);
        return false;
    }

    if (!readNextNonEmptyLine(in, line))
    {
        if (error)
            *error = QStringLiteral("IIIN.DAT 缺少第二行：%1").arg(filePath);
        return false;
    }

    mesh.nodes.clear();
    mesh.nodes.reserve(mesh.nodeCount);
    for (int i = 0; i < mesh.nodeCount; ++i)
    {
        if (!readNextNonEmptyLine(in, line))
        {
            if (error)
                *error = QStringLiteral("IIIN.DAT 节点数据不足：%1").arg(filePath);
            return false;
        }

        QVector<QString> fields = splitFields(line);
        if (fields.size() < mesh.dimension + 1)
        {
            if (error)
                *error = QStringLiteral("IIIN.DAT 节点行格式错误：%1").arg(line);
            return false;
        }

        Node node;
        node.xyz.resize(3);
        node.xyz[0] = fields.at(0).toDouble();
        node.xyz[1] = fields.at(1).toDouble();
        node.xyz[2] = mesh.dimension == 3 ? fields.at(2).toDouble() : 0.0;
        node.id = fields.at(mesh.dimension).toInt();
        if (node.id <= 0)
            node.id = i + 1;
        mesh.nodeIdToOutputIndex.insert(node.id, mesh.nodes.size() + 1);
        mesh.nodes.push_back(node);
    }

    mesh.elements.clear();
    mesh.elements.reserve(std::max(0, mesh.elementCount));
    for (int i = 0; i < mesh.elementCount; ++i)
    {
        if (!readNextNonEmptyLine(in, line))
            break;

        QVector<QString> fields = splitFields(line);
        if (fields.size() < 2)
            continue;

        Element elem;
        elem.id = fields.at(0).toInt();

        const int availableNodeFields = fields.size() - 1;
        const int nodeFields = std::min(mesh.nodesPerElement, availableNodeFields);
        for (int j = 0; j < nodeFields; ++j)
            elem.nodes.push_back(fields.at(j + 1).toInt());

        const int materialIndex = 1 + mesh.nodesPerElement;
        if (fields.size() > materialIndex)
        {
            bool matOk = false;
            const int material = fields.at(materialIndex).toInt(&matOk);
            if (matOk && material != 0)
                elem.material = material;
        }

        if (!elem.nodes.isEmpty())
            mesh.elements.push_back(elem);
    }

    return !mesh.nodes.isEmpty() && !mesh.elements.isEmpty();
}

QString existingFile(const QDir& dir, const QString& name)
{
    const QFileInfo exact(dir.filePath(name));
    if (exact.exists())
        return exact.absoluteFilePath();

    const QFileInfoList entries = dir.entryInfoList(QDir::Files);
    for (const QFileInfo& info : entries)
    {
        if (QString::compare(info.fileName(), name, Qt::CaseInsensitive) == 0)
            return info.absoluteFilePath();
    }
    return QString();
}

QVector<QVector<double>> readNodeValues(const QString& filePath, const Mesh& mesh, int minColumns,
                                        QString* warning)
{
    QVector<QVector<double>> values(mesh.nodes.size());

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        if (warning)
            *warning = QStringLiteral("无法打开结果文件：%1").arg(filePath);
        return values;
    }

    QTextStream in(&file);
    while (!in.atEnd())
    {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty())
            continue;

        QVector<QString> fields = splitFields(line);
        if (fields.size() < minColumns + 1)
            continue;

        bool idOk = false;
        const int nodeId = fields.at(0).toInt(&idOk);
        if (!idOk)
            continue;

        const int outIndex = mesh.nodeIdToOutputIndex.value(nodeId, 0);
        if (outIndex <= 0 || outIndex > values.size())
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
            values[outIndex - 1] = row;
    }

    return values;
}

QString elementType(const Mesh& mesh)
{
    if (mesh.dimension == 3)
        return mesh.nodesPerElement >= 8 ? QStringLiteral("BRICK") : QStringLiteral("TETRAHEDRON");
    return mesh.nodesPerElement == 3 ? QStringLiteral("TRIANGLE") : QStringLiteral("QUADRILATERAL");
}

int tecplotNodeCount(const Mesh& mesh)
{
    if (mesh.dimension == 3)
        return mesh.nodesPerElement >= 8 ? 8 : 4;
    return mesh.nodesPerElement == 3 ? 3 : 4;
}

double safeValue(const QVector<double>& row, int index)
{
    return index >= 0 && index < row.size() ? row.at(index) : 0.0;
}

bool writeTecplot(const QString& filePath, const Mesh& mesh, const QString& title,
                  const QStringList& variables, const QVector<QVector<double>>& values,
                  QString* error)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
    {
        if (error)
            *error = QStringLiteral("无法写入 Tecplot 文件：%1").arg(filePath);
        return false;
    }

    QTextStream out(&file);
    out.setRealNumberNotation(QTextStream::ScientificNotation);
    out.setRealNumberPrecision(8);

    out << "TITLE=\"" << title << "\"\n";
    out << "VARIABLES=";
    QStringList coordVars = mesh.dimension == 3 ? QStringList{"X", "Y", "Z"} : QStringList{"X", "Y"};
    QStringList allVars = coordVars + variables;
    for (int i = 0; i < allVars.size(); ++i)
    {
        if (i > 0)
            out << ", ";
        out << "\"" << allVars.at(i) << "\"";
    }
    out << "\n";
    out << QString("ZONE N=%1,E=%2 ,F=FEPOINT,ET=%3\n")
               .arg(mesh.nodes.size(), 8)
               .arg(mesh.elements.size(), 8)
               .arg(elementType(mesh));

    for (int i = 0; i < mesh.nodes.size(); ++i)
    {
        const Node& node = mesh.nodes.at(i);
        out << QString("%1 %2").arg(node.xyz.at(0), 16, 'E', 8).arg(node.xyz.at(1), 16, 'E', 8);
        if (mesh.dimension == 3)
            out << QString(" %1").arg(node.xyz.at(2), 16, 'E', 8);

        const QVector<double> row = i < values.size() ? values.at(i) : QVector<double>();
        for (int j = 0; j < variables.size(); ++j)
            out << QString(" %1").arg(safeValue(row, j), 16, 'E', 8);
        out << "\n";
    }

    const int count = tecplotNodeCount(mesh);
    for (const Element& elem : mesh.elements)
    {
        for (int i = 0; i < count; ++i)
        {
            int nodeId = i < elem.nodes.size() ? elem.nodes.at(i) : elem.nodes.value(0, 1);
            const int mapped = mesh.nodeIdToOutputIndex.value(nodeId, nodeId);
            out << QString("%1").arg(mapped, 8);
        }
        out << "\n";
    }

    return true;
}

void addGenerated(PostResultGenerator::Result& result, const QString& path)
{
    result.generatedFiles << QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
}

bool generateZones(const QDir& resultDir, const Mesh& mesh, PostResultGenerator::Result& result)
{
    QSet<int> materialSet;
    for (const Element& elem : mesh.elements)
        materialSet.insert(elem.material);

    QList<int> materials = materialSet.values();
    std::sort(materials.begin(), materials.end());
    if (materials.isEmpty())
        materials << 1;

    const QString fileSetPath = resultDir.filePath(QStringLiteral("MAFILESET.DAT"));
    QFile fileSet(fileSetPath);
    if (!fileSet.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
    {
        result.warnings << QStringLiteral("无法写入材料分区索引：%1").arg(fileSetPath);
        return false;
    }

    QTextStream out(&fileSet);
    out << QString("%1  NFILE\n").arg(materials.size(), 12);
    for (int i = 0; i < materials.size(); ++i)
    {
        const QString zoneName = QStringLiteral("ZONE%1").arg(i + 1);
        out << QString("%1 %2 %3 CAFILE   I   MATYPE\n")
                   .arg(zoneName, -10)
                   .arg(i + 1, 6)
                   .arg(materials.at(i), 6);

        const QString zonePath = resultDir.filePath(zoneName + QStringLiteral(".DAT"));
        QFile zoneFile(zonePath);
        if (!zoneFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        {
            result.warnings << QStringLiteral("无法写入材料分区文件：%1").arg(zonePath);
            continue;
        }
        QTextStream zoneOut(&zoneFile);
        zoneOut << QString("%1   NUM\n").arg(1, 12);
        zoneOut << QString("%1 %2\n").arg(materials.at(i), 6).arg(i + 1, 6);
        addGenerated(result, zonePath);
    }

    addGenerated(result, fileSetPath);
    return true;
}

QVector<QVector<double>> buildHeadPressure(const QVector<QVector<double>>& raw)
{
    QVector<QVector<double>> out(raw.size());
    for (int i = 0; i < raw.size(); ++i)
    {
        const QVector<double>& row = raw.at(i);
        out[i] = {safeValue(row, 0), safeValue(row, 1) / 1.0e6};
    }
    return out;
}

QVector<QVector<double>> buildVelocity(const QVector<QVector<double>>& raw, int dimension)
{
    QVector<QVector<double>> out(raw.size());
    const int n = dimension == 3 ? 4 : 3;
    for (int i = 0; i < raw.size(); ++i)
    {
        const double vx = safeValue(raw.at(i), 0);
        const double vy = safeValue(raw.at(i), 1);
        const double vz = dimension == 3 ? safeValue(raw.at(i), 2) : 0.0;
        out[i].reserve(n);
        out[i] << vx << vy;
        if (dimension == 3)
            out[i] << vz;
        out[i] << std::sqrt(vx * vx + vy * vy + vz * vz);
    }
    return out;
}

QVector<QVector<double>> buildDisplacement(const QVector<QVector<double>>& raw, int dimension)
{
    QVector<QVector<double>> out(raw.size());
    for (int i = 0; i < raw.size(); ++i)
    {
        const double ux = safeValue(raw.at(i), 0) * 100.0;
        const double uy = safeValue(raw.at(i), 1) * 100.0;
        const double uz = dimension == 3 ? safeValue(raw.at(i), 2) * 100.0 : 0.0;
        out[i] << ux << uy;
        if (dimension == 3)
            out[i] << uz;
        out[i] << std::sqrt(ux * ux + uy * uy + uz * uz);
    }
    return out;
}

QVector<QVector<double>> buildPrincipalStress(const QVector<QVector<double>>& raw)
{
    QVector<QVector<double>> out(raw.size());
    for (int i = 0; i < raw.size(); ++i)
    {
        const double s1 = safeValue(raw.at(i), 0) / 1.0e6;
        const double s2 = safeValue(raw.at(i), 1) / 1.0e6;
        const double s3 = safeValue(raw.at(i), 2) / 1.0e6;
        const double smax = std::max(s1, std::max(s2, s3));
        const double smin = std::min(s1, std::min(s2, s3));
        const double tmax = (smax - smin) * 0.5;
        const double seq = std::sqrt(((s1 - s2) * (s1 - s2) + (s2 - s3) * (s2 - s3) +
                                      (s3 - s1) * (s3 - s1)) *
                                     0.5);
        out[i] = {s1, s2, s3, tmax, seq};
    }
    return out;
}

QStringList cmmVariables(int count)
{
    static const QStringList known = {
        QStringLiteral("EG_UE"), QStringLiteral("EG_UT"), QStringLiteral("EG_WP"),
        QStringLiteral("SL"),    QStringLiteral("FUN"),   QStringLiteral("ARUPT"),
        QStringLiteral("Kcb"),   QStringLiteral("k")};

    QStringList vars;
    for (int i = 0; i < count; ++i)
        vars << (i < known.size() ? known.at(i) : QStringLiteral("CMM%1").arg(i + 1));
    return vars;
}

QVector<QVector<double>> limitColumns(const QVector<QVector<double>>& raw, int count)
{
    QVector<QVector<double>> out(raw.size());
    for (int i = 0; i < raw.size(); ++i)
    {
        out[i].reserve(count);
        for (int j = 0; j < count; ++j)
            out[i] << safeValue(raw.at(i), j);
    }
    return out;
}

bool generateFromNodeFile(const QDir& outputDir, const Mesh& mesh, const QString& sourceName,
                          const QString& outputName, const QString& title,
                          const QStringList& variables, const QVector<QVector<double>>& values,
                          PostResultGenerator::Result& result)
{
    Q_UNUSED(sourceName);
    QString error;
    const QString outPath = outputDir.filePath(outputName);
    if (!writeTecplot(outPath, mesh, title, variables, values, &error))
    {
        result.warnings << error;
        return false;
    }
    addGenerated(result, outPath);
    return true;
}

} // namespace

PostResultGenerator::Result PostResultGenerator::generate(const Options& options)
{
    Result result;

    QDir resultDir(QDir::fromNativeSeparators(options.resultDir.trimmed()));
    if (!resultDir.exists())
    {
        result.error = QStringLiteral("成果目录不存在：%1").arg(options.resultDir);
        return result;
    }

    const QString meshPath = existingFile(resultDir, QStringLiteral("IIIN.DAT"));
    if (meshPath.isEmpty())
    {
        result.error = QStringLiteral("成果目录中缺少 IIIN.DAT：%1").arg(resultDir.absolutePath());
        return result;
    }

    Mesh mesh;
    QString error;
    if (!parseMesh(meshPath, mesh, &error))
    {
        result.error = error;
        return result;
    }

    const bool useExternalOutputDir = !options.outputDir.trimmed().isEmpty();
    QDir outputDir(useExternalOutputDir
                       ? QDir::fromNativeSeparators(options.outputDir.trimmed())
                       : resultDir.absolutePath());
    if (!outputDir.exists() && !outputDir.mkpath(QStringLiteral(".")))
    {
        result.error = QStringLiteral("无法创建后处理输出目录：%1").arg(outputDir.absolutePath());
        return result;
    }

    auto outputName = [useExternalOutputDir](const QString& legacyName, const QString& internalName)
    {
        return useExternalOutputDir ? internalName : legacyName;
    };

    if (!useExternalOutputDir || options.writeLegacyAuxiliaryFiles)
        generateZones(outputDir, mesh, result);

    const QString hpPath = existingFile(resultDir, QStringLiteral("IIDISP.TXT"));
    const QString velocityPath = existingFile(resultDir, QStringLiteral("INVELO.TXT"));
    const QString displacementPath = existingFile(resultDir, QStringLiteral("IIDISP.TXT"));
    const QString principalPath = existingFile(resultDir, QStringLiteral("MODPRI.TXT"));
    const QString cmmPath = existingFile(resultDir, QStringLiteral("NODCMM.TXT"));

    if (!velocityPath.isEmpty())
    {
        QString warning;
        const auto rawVelocity = readNodeValues(velocityPath, mesh, mesh.dimension, &warning);
        if (!warning.isEmpty())
            result.warnings << warning;
        QStringList vars = mesh.dimension == 3 ? QStringList{"VX", "VY", "VZ", "VABS"}
                                               : QStringList{"VX", "VY", "VABS"};
        generateFromNodeFile(outputDir, mesh, QStringLiteral("INVELO.TXT"),
                             outputName(QStringLiteral("TEC_VELO.DAT"),
                                        QStringLiteral("velocity.tec")),
                             QStringLiteral("VELOCITY"), vars,
                             buildVelocity(rawVelocity, mesh.dimension), result);
    }
    else
    {
        result.skippedFiles << QStringLiteral("TEC_VELO.DAT（缺少 INVELO.TXT）");
    }

    if (!hpPath.isEmpty())
    {
        QString warning;
        const auto rawHead = readNodeValues(hpPath, mesh, 2, &warning);
        if (!warning.isEmpty())
            result.warnings << warning;

        const bool looksLikeSeepage = !velocityPath.isEmpty();
        if (looksLikeSeepage)
        {
            generateFromNodeFile(outputDir, mesh, QStringLiteral("IIDISP.TXT"),
                                 outputName(QStringLiteral("TEC_HP.DAT"),
                                            QStringLiteral("head_pressure.tec")),
                                 QStringLiteral("HP-SEEPAGE (MPa)"),
                                 QStringList{QStringLiteral("HEAD(m)"), QStringLiteral("P(MPa)")},
                                 buildHeadPressure(rawHead), result);
        }
    }

    if (!displacementPath.isEmpty() && velocityPath.isEmpty())
    {
        QString warning;
        const auto rawDisp = readNodeValues(displacementPath, mesh, mesh.dimension, &warning);
        if (!warning.isEmpty())
            result.warnings << warning;

        QStringList vars = mesh.dimension == 3
                               ? QStringList{QStringLiteral("UX(cm)"), QStringLiteral("UY(cm)"),
                                             QStringLiteral("UZ(cm)"), QStringLiteral("UH(cm)")}
                               : QStringList{QStringLiteral("UX(cm)"), QStringLiteral("UY(cm)"),
                                             QStringLiteral("UH(cm)")};
        generateFromNodeFile(outputDir, mesh, QStringLiteral("IIDISP.TXT"),
                             outputName(QStringLiteral("TEC_DISP.DAT"),
                                        QStringLiteral("displacement.tec")),
                             QStringLiteral("DISP (cm)"), vars,
                             buildDisplacement(rawDisp, mesh.dimension), result);
    }
    else if (displacementPath.isEmpty())
    {
        result.skippedFiles << QStringLiteral("TEC_DISP.DAT（缺少 IIDISP.TXT）");
    }

    if (!principalPath.isEmpty())
    {
        QString warning;
        const auto rawPrincipal = readNodeValues(principalPath, mesh, 2, &warning);
        if (!warning.isEmpty())
            result.warnings << warning;

        generateFromNodeFile(outputDir, mesh, QStringLiteral("MODPRI.TXT"),
                             outputName(QStringLiteral("TEC_PRS.DAT"),
                                        QStringLiteral("principal_stress.tec")),
                             QStringLiteral("PRS (MPa)"),
                             QStringList{QStringLiteral("PRS1(MPa)"), QStringLiteral("PRS2(MPa)"),
                                         QStringLiteral("PRS3(MPa)"), QStringLiteral("TMAX(MPa)"),
                                         QStringLiteral("SGMA_EQ")},
                             buildPrincipalStress(rawPrincipal), result);
    }
    else
    {
        result.skippedFiles << QStringLiteral("TEC_PRS.DAT（缺少 MODPRI.TXT）");
    }

    if (!cmmPath.isEmpty())
    {
        QString warning;
        const auto rawCmm = readNodeValues(cmmPath, mesh, 1, &warning);
        if (!warning.isEmpty())
            result.warnings << warning;

        int count = 0;
        for (const QVector<double>& row : rawCmm)
            count = std::max(count, static_cast<int>(row.size()));
        count = std::max(1, std::min(count, 8));

        const QStringList vars = cmmVariables(count);
        const auto values = limitColumns(rawCmm, count);
        if (useExternalOutputDir && !options.writeLegacyAuxiliaryFiles)
        {
            generateFromNodeFile(outputDir, mesh, QStringLiteral("NODCMM.TXT"),
                                 QStringLiteral("stress_level.tec"),
                                 QStringLiteral("STRESS LEVEL"), vars, values, result);
        }
        else
        {
            generateFromNodeFile(outputDir, mesh, QStringLiteral("NODCMM.TXT"),
                                 QStringLiteral("TEC_CMM.DAT"), QStringLiteral("CMM"), vars,
                                 values, result);
            generateFromNodeFile(outputDir, mesh, QStringLiteral("NODCMM.TXT"),
                                 QStringLiteral("TEC_SL.DAT"), QStringLiteral("SL"), vars,
                                 values, result);
        }
    }
    else
    {
        result.skippedFiles << QStringLiteral("TEC_CMM.DAT/TEC_SL.DAT（缺少 NODCMM.TXT）");
    }

    result.ok = !result.generatedFiles.isEmpty();
    if (!result.ok && result.error.isEmpty())
        result.error = QStringLiteral("未找到可转换的半结果文件。");
    return result;
}
