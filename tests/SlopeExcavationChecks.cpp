#include "../plugins/Base_Solver/SlopeExcavationProtocol.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>
#include <vtkCellType.h>
#include <vtkDoubleArray.h>
#include <vtkNew.h>
#include <vtkPoints.h>
static void require(bool value, const char* text)
{
    if (!value)
        throw std::runtime_error(text);
}
static QString read(const QDir& d, const QString& n)
{
    QFile f(d.filePath(n));
    require(f.open(QIODevice::ReadOnly), "read output");
    return QString::fromLatin1(f.readAll());
}
static void check(int dimension, bool native)
{
    QTemporaryDir temporary;
    QDir dir(temporary.path());
    vtkNew<vtkUnstructuredGrid> grid;
    vtkNew<vtkPoints> points;
    const int levels = dimension == 2 ? 1 : 2;
    for (int z = 0; z < levels; ++z)
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 4; ++x)
                points->InsertNextPoint(x, y, z);
    grid->SetPoints(points);
    for (int x = 0; x < 3; ++x)
    {
        vtkIdType ids[8] = {x, x + 1, x + 5, x + 4, x + 8, x + 9, x + 13, x + 12};
        grid->InsertNextCell(dimension == 2 ? VTK_QUAD : VTK_HEXAHEDRON, dimension == 2 ? 4 : 8,
                             ids);
    }
    vtkNew<vtkDoubleArray> elems;
    elems->SetName("elem_id");
    for (int id : {11, 27, 39})
        elems->InsertNextValue(id);
    grid->GetCellData()->AddArray(elems);
    vtkNew<vtkDoubleArray> nodes;
    nodes->SetName("node_id");
    for (vtkIdType i = 0; i < points->GetNumberOfPoints(); ++i)
        nodes->InsertNextValue(101 + i);
    grid->GetPointData()->AddArray(nodes);
    QString input;
    QTextStream out(&input);
    out << points->GetNumberOfPoints() << " " << dimension << " 3 " << (dimension == 2 ? 4 : 8)
        << " 3 1 4 6 4\n 1 2\n";
    for (vtkIdType i = 0; i < points->GetNumberOfPoints(); ++i)
    {
        double p[3];
        points->GetPoint(i, p);
        out << 101 + i << " " << p[0] << " " << p[1];
        if (dimension == 3)
            out << " " << p[2];
        out << "\n";
    }
    for (int i = 0; i < 3; ++i)
    {
        auto* c = grid->GetCell(i);
        out << (native ? int(elems->GetValue(i)) : i + 1);
        for (vtkIdType j = 0; j < c->GetNumberOfPoints(); ++j)
            out << " " << 101 + c->GetPointId(j);
        out << " 6 3 99\n";
    }
    const auto bytes = input.toLatin1();
    QFile file(dir.filePath("IIIN.DAT"));
    require(file.open(QIODevice::WriteOnly), "write input");
    file.write(bytes);
    file.close();
    SlopeExcavationProtocol::Prepared prepared;
    QString error;
    require(SlopeExcavationProtocol::prepare(dir, grid, dimension, native, {{11}, {27}}, prepared,
                                             error),
            qPrintable(error));
    require(prepared.layers[0] == -1 && prepared.layers[1] == -2 && prepared.layers[2] == 1,
            "stage layers and retained model");
    const auto a = read(dir, "IIKE_E01-01.DAT");
    require(a.contains(QString::number(native ? 11 : 1) + " 1"),
            "source-to-calculation element mapping");
    const auto b = read(dir, "IIJX_T01-02.DAT");
    const auto rows = b.split('\n');
    require(rows[0].section(' ', 0, 0).toInt() == (dimension == 2 ? 6 : 10),
            "shared faces removed in cumulative excavation");
    require(b.contains("IEX1=-1 IEX2=-2"), "cumulative levels");
    for (int i = 2; i < 2 + (dimension == 2 ? 6 : 10); ++i)
    {
        const auto row = rows[i].simplified().split(' ');
        require(row[2].toInt() < 0, "ILAY stored instead of adjacent element");
        require(row[3].toInt() >= 101, "native node ids preserved");
    }
    require(read(dir, "RELE_KETOKE.DAT").contains("NUMNP MALIA NLIA_MAX IP"),
            "element connectivity exported");
    require(prepared.faces.size() == (dimension == 2 ? 6 : 10), "support boundary samples");
    for (const QString& name : {QStringLiteral("ALFAIE.DAT"), QStringLiteral("ALFAJD.DAT")})
    {
        QFile field(dir.filePath(name));
        require(field.open(QIODevice::WriteOnly), "write support field");
        QTextStream stream(&field);
        const int count = name == "ALFAIE.DAT" ? 3 : int(points->GetNumberOfPoints());
        stream << count << " 5\n ID VALUES\n";
        for (int i = 0; i < count; ++i)
            stream << i + 1 << " 1.25 2.5 0 0 0\n";
    }
    require(SlopeExcavationProtocol::remapReinforcementIds(dir, grid, prepared, error),
            qPrintable(error));
    const auto cellRows = read(dir, "ALFAIE.DAT").split('\n');
    for (int i = 0; i < 3; ++i)
        require(
            cellRows[i + 2].simplified() ==
                QStringLiteral("%1 1.25 2.5 0 0 0").arg(native ? int(elems->GetValue(i)) : i + 1),
            "support element IDs follow calculation numbering without changing field values");
    const auto nodeRows = read(dir, "ALFAJD.DAT").split('\n');
    for (vtkIdType i = 0; i < points->GetNumberOfPoints(); ++i)
        require(nodeRows[i + 2].simplified() == QStringLiteral("%1 1.25 2.5 0 0 0").arg(101 + i),
                "support node IDs preserve source numbering");
    require(!SlopeExcavationProtocol::prepare(dir, grid, dimension, native, {{11}, {11}}, prepared,
                                              error),
            "overlap rejected");
    require(!SlopeExcavationProtocol::prepare(dir, grid, dimension, native, {{11}, {}}, prepared,
                                              error),
            "empty stage rejected");
    require(!SlopeExcavationProtocol::prepare(dir, grid, dimension, native, {{11}, {27, 39}},
                                              prepared, error),
            "entire model cannot be excavated");
    require(
        !SlopeExcavationProtocol::prepare(dir, grid, dimension, native, {{999}}, prepared, error),
        "stale selection rejected");
}
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try
    {
        for (int dim : {2, 3})
            for (bool native : {false, true})
                check(dim, native);
        std::cout << "PASS: 2D/3D, source/renumbered IDs, stages, shared faces, retained regions "
                     "and invalid selections\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
