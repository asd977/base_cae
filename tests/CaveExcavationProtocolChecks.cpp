#include "plugins/Base_Solver/CaveExcavationProtocol.h"
#include <QCoreApplication>
#include <QHash>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

static void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

static QByteArray read(const QString& path)
{
    QFile f(path);
    require(f.open(QIODevice::ReadOnly), "read failed");
    return f.readAll();
}

static void write(const QString& path, const QByteArray& data)
{
    QFile f(path);
    require(f.open(QIODevice::WriteOnly) && f.write(data) == data.size(), "write failed");
}

static void rewriteFaces(const QDir& input)
{
    const auto split = [](const QString& line) {
        return line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    };
    const auto mesh = QString::fromLocal8Bit(read(input.filePath("IIIN.DAT"))).split('\n');
    const auto header = split(mesh[0]);
    const int start = 2 + header[0].toInt();
    const int count = header[2].toInt();
    const int nodeCount = header[3].toInt();
    QHash<int, int> layers;
    for (int i = start; i < start + count; ++i)
    {
        const auto row = split(mesh[i]);
        layers.insert(row[0].toInt(), row[nodeCount + 3].toInt());
    }
    for (const QString& name : input.entryList({"IIJX_E*.DAT", "IIJX_T*.DAT"}, QDir::Files))
    {
        const auto lines = QString::fromLocal8Bit(read(input.filePath(name))).split('\n');
        const auto header = split(lines[0]);
        const int count = header[0].toInt();
        const int nodes = header[1].toInt() - 2;
        QString text;
        QTextStream out(&text);
        out << lines[0] << "\nKJX KE1 ILAY NODES JAT\n";
        for (int i = 0; i < count; ++i)
        {
            const auto row = split(lines[i + 2]);
            const int element = row[1].toInt();
            require(layers.contains(element) && layers[element] < 0, "invalid excavation layer");
            QVector<int> ids;
            for (int n = 0; n < nodes; ++n)
                ids.append(row[n + 3].toInt());
            CaveExcavationProtocol::writeFace(out, i + 1, element, layers[element], ids,
                                               row.value(nodes + 3).toInt());
        }
        write(input.filePath(name), text.toLocal8Bit());
    }
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try
    {
        require(CaveExcavationProtocol::dynamicStateSubdirectory(false,false,true,false,2,false)=="CASE01EXCA/E02","dynamic state must be the final excavation stage");
        require(CaveExcavationProtocol::dynamicStateSubdirectory(false,true,true,true,3,true)=="CASE01JACA/J03PART","crane release must precede dynamics");
        require(CaveExcavationProtocol::dynamicStateSubdirectory(true,true,true,true,2,true)=="CASE01LINE","lining is the last static task");
        require(CaveExcavationProtocol::dynamicStateSubdirectory(false,false,false,false,1,false)=="CASE01EALL","whole excavation result");
        const auto args = app.arguments();
        if (args.size() == 3 && args[1] == "--rewrite-faces")
        {
            rewriteFaces(QDir(args[2]));
            return 0;
        }
        if (args.size() == 5 && args[1] == "--prepare-elements")
        {
            QString error;
            if (!CaveExcavationProtocol::prepareElementInputs(
                    QDir(args[2]), QDir(args[3]), args[4].toInt(), &error))
                throw std::runtime_error(error.toStdString());
            return 0;
        }
        for (const QVector<int>& nodes : {QVector<int>{146, 178}, QVector<int>{4, 7, 9},
                                         QVector<int>{4, 7, 9, 12}})
        {
            QString record;
            QTextStream out(&record);
            CaveExcavationProtocol::writeFace(out, 1, 3, -2, nodes, 0);
            const auto fields = record.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            require(fields[2] == "-2" && fields.size() == nodes.size() + 4,
                    "face association must contain negative layer");
            for (int i = 0; i < nodes.size(); ++i)
                require(fields[i + 3].toInt() == nodes[i], "face orientation changed");
        }
        QTemporaryDir dir;
        QDir root(dir.path()), input(root.filePath("input"));
        require(root.mkpath("input"), "mkdir failed");
        QString error;
        require(!CaveExcavationProtocol::prepareElementInputs(input, root, 3, &error),
                "missing list must fail");
        const QString name = "IIKE_T01-03.DAT";
        write(input.filePath(name), "2 NUMKE\n3 1\n6 2\n");
        require(CaveExcavationProtocol::prepareElementInputs(input, root, 3, &error),
                "initial staging failed");
        write(root.filePath("CASE01EALL/IIDISP.TXT"), "existing-result");
        write(input.filePath(name), "1 NUMKE\n9 1\n");
        require(CaveExcavationProtocol::prepareElementInputs(input, root, 3, &error),
                "repeated staging failed");
        for (const QString& task : {QString("CASE01EALL"), QString("CASE01JALL")})
        {
            require(read(root.filePath(task + '/' + name)) == read(input.filePath(name)),
                    "stale canonical list");
            require(QFile::exists(root.filePath(task + "/FLAG_DIR.DAT")), "missing NEWDIR flag");
        }
        require(read(input.filePath(name + ".DAT")) == read(input.filePath(name)), "stale alias");
        require(read(root.filePath("CASE01EALL/IIDISP.TXT")) == "existing-result",
                "staging modified result");
        std::cout << "PASS: face protocol, repeatable legacy element staging\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
