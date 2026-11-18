#include "../plugins/Base_Mesh/MeshGridIO.h"
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>
#include <vtkCellData.h>
#include <vtkDataArray.h>
#include <vtkStringArray.h>
#include <vtkUnstructuredGrid.h>
#include <iostream>
#include <stdexcept>

static void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

static QString fixture(int property, bool components)
{
    QString text;
    QTextStream out(&text);
    out << "BEGIN BULK\n";
    for (int z = 0; z < 2; ++z)
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 4; ++x)
                out << "GRID," << 100 + x + 4*y + 8*z << ",," << x << ',' << y << ',' << z << '\n';
    for (int x = 0; x < 3; ++x)
    {
        out << "CHEXA," << (x == 0 ? 11 : x == 1 ? 20 : 300) << ',' << property;
        for (int node : {x, x+1, x+5, x+4, x+8, x+9, x+13, x+12}) out << ',' << 100+node;
        out << '\n';
    }
    out << "CQUAD4,101," << property << ",100,101,105,104\n";
    out << "CQUAD4,201," << property << ",102,103,107,106\n";
    if (components)
        out << "$HMMOVE        7\n$             11THRU\n$             20\n"
               "$HMMOVE        9\n$            300     201\n$\n"
               "$HMMOVE        7\n$            101\n"
               "$HMNAME COMP                   7\"左侧岩体\"\n"
               "$HMNAME COMP                   9\"右侧岩体\"\n";
    else
        out << "$HMNAME COMP                   7\"没有归属列表\"\n";
    out << "ENDDATA\n";
    return text;
}

static void checkFixture(int property, bool components)
{
    QTemporaryDir temporary;
    QFile file(temporary.filePath("groups.nas"));
    require(file.open(QIODevice::WriteOnly), "write fixture");
    file.write(fixture(property, components).toUtf8());
    file.close();
    QString error;
    auto grid = MeshGridIO::instance().importNastran(file.fileName(), &error);
    require(grid != nullptr, qPrintable(error));
    require(grid->GetNumberOfCells() == 3, "surface cards must remain boundaries, not duplicate volume cells");
    const auto& map = MeshGridIO::instance().getMeshMapRef();
    auto* parts = grid->GetCellData()->GetArray("src_part");
    auto* materials = grid->GetCellData()->GetArray("MATYPE");
    if (components && property == 0)
        require(materials->GetTuple1(0) == materials->GetTuple1(1) &&
                materials->GetTuple1(0) != materials->GetTuple1(2), "PID=0 components available through existing material groups");
    else
        for (int i = 0; i < 3; ++i)
            require(materials->GetTuple1(i) == materials->GetTuple1(0), "existing property grouping retained");
    if (!components)
    {
        require(map.materialGroupCategory == "Physical", "legacy PID grouping retained");
        require(parts->GetTuple1(0) == property, "legacy source property unchanged");
        for (const auto& set : map.tagSets) require(set.key.category != "REGION", "names without membership do not create groups");
        return;
    }
    require(map.materialGroupCategory == "REGION" && map.excavationGroupCategory == "REGION", "components selectable for materials and excavation");
    require(parts->GetTuple1(0) == 7 && parts->GetTuple1(1) == 7 && parts->GetTuple1(2) == 9, "THRU, repeated blocks and sparse source IDs");
    auto* properties = grid->GetCellData()->GetArray("nas_property_id");
    for (int i = 0; i < 3; ++i) require(properties->GetTuple1(i) == property, "source PID retained separately");
    require(map.tagSets.value(MeshTagKey{"REGION",3,7}).cellIds.size() == 2, "first volume component");
    require(map.tagSets.value(MeshTagKey{"REGION",3,9}).cellIds.size() == 1, "second volume component");
    require(map.tagSets.value(MeshTagKey{"REGION",3,7}).name == QStringLiteral("左侧岩体"), "late Unicode component names");
    int componentBoundaries = 0;
    for (const auto& set : map.tagSets)
        if (set.key.dim == 2 && set.source == "HyperMeshComponent")
        {
            ++componentBoundaries;
            require(set.faceKeys.size() == 1 && set.pointIds.size() == 4, "component boundary face and node selection");
        }
    require(componentBoundaries == 2, "surface components preserved");
}

static void checkModel(const QString& path)
{
    QString error;
    auto grid = MeshGridIO::instance().importNastran(path, &error);
    require(grid != nullptr, qPrintable(error));
    require(grid->GetNumberOfPoints() == 54789 && grid->GetNumberOfCells() == 52220, "model2 node and volume counts");
    const auto& map = MeshGridIO::instance().getMeshMapRef();
    const int expected[] = {39500,10160,2560};
    int faces = 0, boundaryGroups = 0;
    for (int i = 0; i < 3; ++i)
    {
        const auto set = map.tagSets.value(MeshTagKey{"REGION",3,i+1});
        require(set.cellIds.size() == expected[i], "model2 component membership counts");
        require(set.name == QString::number(i+1), "model2 component names");
    }
    for (const auto& set : map.tagSets)
        if (set.key.dim == 2 && set.source == "HyperMeshComponent")
        {
            ++boundaryGroups;
            faces += set.faceKeys.size();
        }
    require(boundaryGroups == 3 && faces == 2611, "model2 surface component groups and all face keys");
    std::cout << "PASS: model2, 54789 nodes, 52220 volume cells; components 39500/10160/2560; 2611 boundary faces\n";
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc,argv);
    try
    {
        for (int property : {0,5}) for (bool components : {false,true}) checkFixture(property,components);
        if (argc > 1) checkModel(app.arguments()[1]);
        std::cout << "PASS: HyperMesh components, ranges, late names, boundaries and legacy PID imports\n";
        return 0;
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
