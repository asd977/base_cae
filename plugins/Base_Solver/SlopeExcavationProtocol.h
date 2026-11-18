#pragma once
#include "CaveExcavationProtocol.h"
#include "CaveReinforcementFieldPage.h"
#include <QHash>
#include <QMap>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <algorithm>
#include <vtkCell.h>
#include <vtkCellData.h>
#include <vtkDataArray.h>
#include <vtkPointData.h>
#include <vtkUnstructuredGrid.h>

namespace SlopeExcavationProtocol
{
struct Prepared
{
    QHash<vtkIdType, int> layers;
    QHash<vtkIdType, int> calculationIds;
    QVector<ReinforcementFaceSample> faces;
};
inline bool remapReinforcementIds(const QDir& input, vtkUnstructuredGrid* grid,
                                  const Prepared& prepared, QString& error)
{
    QMap<vtkIdType, int> ordered;
    for (auto it = prepared.calculationIds.cbegin(); it != prepared.calculationIds.cend(); ++it)
        ordered.insert(it.key(), it.value());
    const auto ids = ordered.values();
    auto* nodes = grid->GetPointData()->GetArray("node_id");
    if (!nodes)
        nodes = grid->GetPointData()->GetArray("nodeId");
    if (!nodes)
        nodes = grid->GetPointData()->GetArray("point_id");
    if (!nodes)
        nodes = grid->GetPointData()->GetArray("global_node_id");
    for (const QString& name : {QStringLiteral("ALFAIE.DAT"), QStringLiteral("ALFAJD.DAT")})
    {
        QFile source(input.filePath(name));
        if (!source.open(QIODevice::ReadOnly))
        {
            error = QStringLiteral("无法读取加固参数场。");
            return false;
        }
        auto lines = QString::fromLatin1(source.readAll()).split('\n');
        source.close();
        const int count =
            name == QStringLiteral("ALFAIE.DAT") ? ids.size() : int(grid->GetNumberOfPoints());
        if (lines.size() < count + 2)
        {
            error = QStringLiteral("加固参数场与当前网格不一致。");
            return false;
        }
        for (int i = 0; i < count; ++i)
        {
            const QRegularExpression number(QStringLiteral("^\\s*\\d+"));
            const auto match = number.match(lines[i + 2]);
            if (!match.hasMatch())
            {
                error = QStringLiteral("加固参数场格式不完整。");
                return false;
            }
            const int id = name == QStringLiteral("ALFAIE.DAT")
                               ? ids[i]
                               : (nodes ? int(nodes->GetTuple1(i)) : i + 1);
            lines[i + 2].replace(match.capturedStart(), match.capturedLength(),
                                 QString::number(id));
        }
        const auto bytes = lines.join('\n').toLatin1();
        QSaveFile output(input.filePath(name));
        if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() ||
            !output.commit())
        {
            error = QStringLiteral("无法保存加固参数场编号。");
            return false;
        }
    }
    return true;
}
inline bool prepare(const QDir& input, vtkUnstructuredGrid* grid, int dimension,
                    bool preserveElementIds, const QVector<QVector<int>>& stages,
                    Prepared& prepared, QString& error)
{
    auto fail = [&](const QString& message)
    {
        error = message;
        return false;
    };
    prepared = {};
    if (!grid || stages.isEmpty())
        return fail(QStringLiteral("请设置开挖级并选择开挖区域。"));
    auto* elementIds = grid->GetCellData()->GetArray("elem_id");
    auto* nodeIds = grid->GetPointData()->GetArray("node_id");
    if (!nodeIds)
        nodeIds = grid->GetPointData()->GetArray("nodeId");
    if (!nodeIds)
        nodeIds = grid->GetPointData()->GetArray("point_id");
    if (!nodeIds)
        nodeIds = grid->GetPointData()->GetArray("global_node_id");
    QHash<int, int> sourceToCalculation;
    QMap<int, vtkIdType> cells;
    int ordinal = 0;
    for (vtkIdType i = 0; i < grid->GetNumberOfCells(); ++i)
    {
        auto* cell = grid->GetCell(i);
        const int dim = cell->GetCellDimension();
        if (dim != dimension && dim != 1)
            continue;
        ++ordinal;
        prepared.layers.insert(i, 1);
        const int sourceId = elementIds ? int(elementIds->GetTuple1(i)) : int(i + 1);
        prepared.calculationIds.insert(i, preserveElementIds ? sourceId : ordinal);
        if (dim != dimension)
            continue;
        const int source = elementIds ? int(elementIds->GetTuple1(i)) : int(i + 1);
        if (source <= 0 || sourceToCalculation.contains(source))
            return fail(QStringLiteral("网格单元编号无效或重复。"));
        const int calculation = preserveElementIds ? source : ordinal;
        sourceToCalculation.insert(source, calculation);
        cells.insert(calculation, i);
    }
    QVector<QSet<int>> stageElements;
    QHash<int, int> layers;
    for (int i = 0; i < stages.size(); ++i)
    {
        if (stages[i].isEmpty())
            return fail(QStringLiteral("第 %1 级没有选择开挖区域。").arg(i + 1));
        QSet<int> selected;
        for (int source : stages[i])
        {
            if (!sourceToCalculation.contains(source))
                return fail(
                    QStringLiteral("第 %1 级的开挖区域与当前网格不一致，请重新选择。").arg(i + 1));
            const int id = sourceToCalculation[source];
            if (selected.contains(id))
                continue;
            if (layers.contains(id))
                return fail(QStringLiteral("同一网格区域不能分配到多个开挖级。"));
            selected.insert(id);
            layers.insert(id, -i - 1);
            prepared.layers[cells[id]] = -i - 1;
        }
        stageElements.push_back(selected);
    }
    if (layers.size() == cells.size())
        return fail(QStringLiteral("请保留开挖后的边坡实体，不能选择全部实体作为开挖区域。"));
    QFile source(input.filePath("IIIN.DAT"));
    if (!source.open(QIODevice::ReadOnly))
        return fail(QStringLiteral("无法读取计算网格。"));
    auto lines = QString::fromLocal8Bit(source.readAll()).split('\n');
    source.close();
    auto split = [](const QString& s)
    {
        return s.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    };
    if (lines.size() < 2)
        return fail(QStringLiteral("计算网格头不完整。"));
    const auto header = split(lines[0]);
    if (header.size() < 4)
        return fail(QStringLiteral("计算网格头不完整。"));
    const int start = 2 + header[0].toInt(), count = header[2].toInt(), width = header[3].toInt();
    if (start < 2 || count < 1 || width < 1 || lines.size() < start + count)
        return fail(QStringLiteral("计算网格数据不完整。"));
    QSet<int> exported;
    for (int i = 0; i < count; ++i)
    {
        auto row = split(lines[start + i]);
        if (row.size() <= width + 3)
            return fail(QStringLiteral("计算网格单元记录不完整。"));
        const int id = row[0].toInt();
        exported.insert(id);
        row[width + 3] = QString::number(layers.value(id, 1));
        lines[start + i] = row.join(' ');
    }
    for (auto it = layers.cbegin(); it != layers.cend(); ++it)
        if (!exported.contains(it.key()))
            return fail(QStringLiteral("开挖单元编号与计算网格不一致。"));
    struct Face
    {
        int element;
        QVector<vtkIdType> points;
    };
    QMap<QVector<vtkIdType>, QVector<Face>> owners;
    for (auto it = cells.cbegin(); it != cells.cend(); ++it)
    {
        auto* cell = grid->GetCell(it.value());
        const int n = dimension == 2 ? cell->GetNumberOfEdges() : cell->GetNumberOfFaces();
        for (int f = 0; f < n; ++f)
        {
            auto* face = dimension == 2 ? cell->GetEdge(f) : cell->GetFace(f);
            if (!face)
                continue;
            QVector<vtkIdType> nodes;
            for (vtkIdType j = 0; j < face->GetNumberOfPoints(); ++j)
                nodes.push_back(face->GetPointId(j));
            if (nodes.size() != (dimension == 2 ? 2 : 3) &&
                nodes.size() != (dimension == 2 ? 2 : 4))
                return fail(QStringLiteral("当前开挖面只支持线性三角形、四边形及其实体网格。"));
            auto key = nodes;
            std::sort(key.begin(), key.end());
            owners[key].push_back({it.key(), nodes});
        }
    }
    auto write = [&](const QString& name, const QString& text)
    {
        QSaveFile f(input.filePath(name));
        const auto bytes = text.toLocal8Bit();
        return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size() && f.commit();
    };
    struct OutputFace
    {
        int element, layer;
        QVector<int> nodes;
        std::array<double, 3> center;
    };
    auto facesFor = [&](const QSet<int>& selected)
    {
        QVector<OutputFace> result;
        for (auto it = owners.cbegin(); it != owners.cend(); ++it)
        {
            int included = 0;
            Face owner;
            for (const auto& candidate : it.value())
                if (selected.contains(candidate.element))
                {
                    ++included;
                    owner = candidate;
                }
            if (included != 1)
                continue;
            OutputFace face;
            face.element = owner.element;
            face.layer = layers.value(owner.element);
            face.center = {{0, 0, 0}};
            for (auto point : owner.points)
            {
                face.nodes.push_back(nodeIds ? int(nodeIds->GetTuple1(point)) : int(point + 1));
                double xyz[3];
                grid->GetPoint(point, xyz);
                for (int k = 0; k < 3; ++k)
                    face.center[k] += xyz[k] / owner.points.size();
            }
            result.push_back(face);
        }
        return result;
    };
    auto writeSet = [&](const QString& code, const QSet<int>& selected, int first, int last)
    {
        QString text;
        QTextStream out(&text);
        auto ids = selected.values();
        std::sort(ids.begin(), ids.end());
        out << ids.size() << " NUMKE\n";
        for (int i = 0; i < ids.size(); ++i)
            out << ids[i] << " " << i + 1 << "\n";
        out << " DIR=" << QDir::toNativeSeparators(input.absolutePath()) << QDir::separator()
            << "\n ISHIFT=1\n NJX=0\n IEX1=" << first << " IEX2=" << last << "\n";
        if (!write("IIKE_" + code + ".DAT", text))
            return false;
        const auto faces = facesFor(selected);
        int nodeWidth = dimension == 2 ? 2 : 3;
        for (const auto& face : faces)
            nodeWidth = qMax(nodeWidth, int(face.nodes.size()));
        text.clear();
        out.seek(0);
        out << faces.size() << " " << nodeWidth + 2 << " NJX MAKOD\n KJX KE1 ILAY NODES JAT\n";
        for (int i = 0; i < faces.size(); ++i)
        {
            auto nodes = faces[i].nodes;
            while (nodes.size() < nodeWidth)
                nodes.push_back(nodes.back());
            CaveExcavationProtocol::writeFace(out, i + 1, faces[i].element, faces[i].layer, nodes,
                                              0);
        }
        out << " DIR=" << QDir::toNativeSeparators(input.absolutePath()) << QDir::separator()
            << "\n ISHIFT=2\n NJX=" << faces.size() << "\n IEX1=" << first << " IEX2=" << last
            << "\n";
        return write("IIJX_" + code + ".DAT", text);
    };
    if (!write("IIIN.DAT", lines.join('\n')))
        return fail(QStringLiteral("无法写入开挖层号。"));
    // The native excavation solver uses point-sharing element neighbourhoods.
    QMap<int, vtkIdType> allCells;
    QHash<vtkIdType, QSet<int>> pointCells;
    for (auto it = prepared.calculationIds.cbegin(); it != prepared.calculationIds.cend(); ++it)
    {
        allCells.insert(it.value(), it.key());
        auto* cell = grid->GetCell(it.key());
        for (vtkIdType j = 0; j < cell->GetNumberOfPoints(); ++j)
            pointCells[cell->GetPointId(j)].insert(it.value());
    }
    QMap<int, QList<int>> neighbours;
    int maximum = 0;
    for (auto it = allCells.cbegin(); it != allCells.cend(); ++it)
    {
        QSet<int> connected;
        auto* cell = grid->GetCell(it.value());
        for (vtkIdType j = 0; j < cell->GetNumberOfPoints(); ++j)
            connected.unite(pointCells[cell->GetPointId(j)]);
        connected.remove(it.key());
        auto ids = connected.values();
        std::sort(ids.begin(), ids.end());
        maximum = qMax(maximum, int(ids.size()));
        neighbours.insert(it.key(), ids);
    }
    QString adjacency;
    QTextStream graph(&adjacency);
    graph << allCells.size() << " " << maximum << " " << maximum << " " << allCells.size()
          << " NUMNP MALIA NLIA_MAX IP\n NJM LJM ELEMENT\n";
    for (auto it = neighbours.cbegin(); it != neighbours.cend(); ++it)
    {
        graph << it.value().size();
        for (int i = 0; i < maximum; ++i)
            graph << " " << it.value().value(i, 0);
        graph << " " << it.key() << "\n";
    }
    if (!write("RELE_KETOKE.DAT", adjacency))
        return fail(QStringLiteral("无法写入开挖单元邻接关系。"));
    QSet<int> cumulative;
    for (int i = 0; i < stageElements.size(); ++i)
    {
        const auto code = QStringLiteral("%1").arg(i + 1, 2, 10, QLatin1Char('0'));
        cumulative.unite(stageElements[i]);
        if (!writeSet("E" + code + "-" + code, stageElements[i], -i - 1, -i - 1) ||
            !writeSet("T01-" + code, cumulative, -1, -i - 1))
            return fail(QStringLiteral("无法写入第 %1 级开挖文件。").arg(i + 1));
    }
    for (const auto& face : facesFor(cumulative))
        prepared.faces.push_back({face.center, face.layer});
    return true;
}
} // namespace SlopeExcavationProtocol
