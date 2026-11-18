#include "../plugins/Base_PostProcessing/PostResultAutoPrepare.h"
#include "../plugins/Base_PostProcessing/PostResultGenerator.h"
#include "../plugins/Base_PostProcessing/PostTecplotUtils.h"
#include "../plugins/Base_PostProcessing/ResultDisplayNames.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextStream>
#include <vtkDataArray.h>
#include <vtkPointData.h>
#include <vtkUnstructuredGrid.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok, const QString& message)
{
    if (!ok) throw std::runtime_error(message.toUtf8().constData());
}

void write(const QString& path, const QByteArray& data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    require(file.open(QIODevice::WriteOnly) && file.write(data) == data.size(), path);
}

QHash<int, double> rawFun(const QString& path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), path);
    QHash<int, double> values;
    QTextStream stream(&file);
    while (!stream.atEnd()) {
        const auto fields = stream.readLine().simplified().split(' ');
        if (fields.size() < 6) continue;
        bool idOk = false, valueOk = false;
        const int id = fields[0].toInt(&idOk);
        const double value = fields[5].toDouble(&valueOk);
        if (idOk && valueOk && std::isfinite(value)) values[id] = value;
    }
    return values;
}

int verifyFiles(const QString& project, const QString& source)
{
    const auto files = PostResultAutoPrepare::resultFiles(project, source, false);
    int funFiles = 0;
    qint64 checkedPoints = 0;
    for (const auto& file : files) {
        if (file.kind != QStringLiteral("material_function")) continue;
        require(file.displayName.contains("FUN"), "FUN missing from result selector label");
        QString suffix = QFileInfo(file.filePath).completeBaseName().mid(QStringLiteral("material_function").size());
        suffix.remove(QStringLiteral("_with_excavated"));
        const QString nativeName = QStringLiteral("NODCMM") + suffix + QStringLiteral(".TXT");
        QString nativePath;
        for (const auto& name : QDir(source).entryList(QDir::Files))
            if (name.compare(nativeName, Qt::CaseInsensitive) == 0) nativePath = QDir(source).filePath(name);
        require(!nativePath.isEmpty(), nativeName);
        const auto expected = rawFun(nativePath);
        QString error;
        const auto grid = PostTecplotUtils::readGrid(file.filePath, &error);
        require(grid && grid->GetNumberOfPoints() > 0, error);
        auto* ids = grid->GetPointData()->GetArray("node_id");
        auto* fun = grid->GetPointData()->GetArray("FUN");
        require(ids && fun, "FUN/node_id missing from displayed VTM");
        for (vtkIdType i = 0; i < grid->GetNumberOfPoints(); ++i) {
            const int id = int(ids->GetComponent(i, 0));
            require(expected.contains(id), QStringLiteral("Unexpected node %1").arg(id));
            const double value = fun->GetComponent(i, 0), original = expected[id];
            require(std::abs(value - original) <= 1e-12 * std::max(1.0, std::abs(original)),
                    QStringLiteral("FUN changed at node %1").arg(id));
            ++checkedPoints;
        }
        if (!file.frameKey.isEmpty()) {
            const auto frameFiles = PostResultAutoPrepare::resultFilesForFrame(project, source, file.frameKey, false);
            require(std::any_of(frameFiles.cbegin(), frameFiles.cend(), [&](const auto& f) {
                return f.kind == file.kind && f.frameKey == file.frameKey;
            }), "FUN missing from condition/frame selector");
        }
        ++funFiles;
    }
    require(funFiles > 0, "No FUN result files");
    std::cout << "FUN_FILES=" << funFiles << " CHECKED_POINTS=" << checkedPoints << std::endl;
    return funFiles;
}

void fixture(int dimension, bool hasFun)
{
    QTemporaryDir temporary;
    require(temporary.isValid(), "Temporary directory");
    QDir project(temporary.path());
    const QString source = project.filePath("CASE01");
    const QByteArray mesh = dimension == 2
        ? "4 2 1 4 3\n8 2\n0 0 10\n1 0 20\n1 1 30\n0 1 40\n1 10 20 30 40 3 2 1\n"
        : "8 3 1 8 3\n8 3\n0 0 0 10\n1 0 0 20\n1 1 0 30\n0 1 0 40\n0 0 1 50\n1 0 1 60\n1 1 1 70\n0 1 1 80\n1 10 20 30 40 50 60 70 80 6 2 1\n";
    write(QDir(source).filePath("IIIN.DAT"), mesh);
    QByteArray values = "NUMNP NCOM\nJD V1 V2 V3 SL FUN ARUPT KCB U-PORE\n";
    // Reverse row order and non-contiguous node IDs detect positional mapping errors.
    for (int i = dimension == 2 ? 4 : 8; i >= 1; --i) {
        const QByteArray function = i == 1 ? "-982.2778" : i == 2 ? "0" : "0.09560704";
        values += QByteArray::number(i * 10) + " 11 22 33 0.75";
        if (hasFun) values += " " + function + " 0 1.333 0";
        values += "\n";
    }
    write(QDir(source).filePath("NODCMM.TXT"), values);
    write(QDir(source).filePath("NODCMM_CZ100CB120.TXT"), values);
    write(QDir(source).filePath("IKEY.DAT"), "ISOLVE METHOD LX_CUBEI ITER METHOD_ITER TOL_ITER\n1 2 3 100 0 0.001\n");
    const QString prepared = PostResultAutoPrepare::preparedDirectory(project.path(), source);
    // Simulate an old result cache: opening it must add FUN without a solver rerun.
    write(QDir(prepared).filePath("manifest.json"), "{\"version\":26,\"files\":[]}");
    const auto files = PostResultAutoPrepare::resultFiles(project.path(), source, false);
    if (hasFun) {
        require(verifyFiles(project.path(), source) == 2, "Missing base/strength-reduction FUN output");
    } else {
        require(std::none_of(files.cbegin(), files.cend(), [](const auto& f) {
            return f.kind == QStringLiteral("material_function");
        }), "Invented FUN values for four-column legacy data");
        require(std::any_of(files.cbegin(), files.cend(), [](const auto& f) {
            return f.kind == QStringLiteral("stress_level");
        }), "Legacy SL output lost");
    }
}
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try {
        const auto args = app.arguments();
        if (args.size() == 3) {
            require(PostResultAutoPrepare::prepareDirectory(args[1], args[2]), "Actual result preparation failed");
            verifyFiles(args[1], args[2]);
        } else {
            fixture(2, true);
            fixture(3, true);
            fixture(2, false);
            require(!ResultDisplayNames::isInternalField("FUN"), "FUN hidden from field selector");
            require(ResultDisplayNames::displayName("FUN").contains("FUN"), "FUN field label");
            require(ResultDisplayNames::scalarBarName("FUN") == "FUN", "FUN legend label");
        }
        std::cout << "FUN_CHECKS_PASSED" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return 1;
    }
}
