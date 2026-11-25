#include "../plugins/Base_Solver/SlopeStandardDatModel.h"

#include <QCoreApplication>
#include <QFile>
#include <iostream>
#include <cmath>

namespace
{
bool near(double a, double b) { return std::abs(a - b) < 1.0e-7; }
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    SlopeStandardDatModel gis;
    gis.nodes = {{1, 510261.375, 2262.5690375}, {2, 510965.0625, 2403.37933268}};
    if (!gis.ensureSearchCircleNearSection()) return 1;
    const auto repaired = gis;
    if (gis.ensureSearchCircleNearSection()) return 2;
    SlopeStandardDatModel local;
    local.nodes = {{1, 0, 0}, {2, 703.6875, 140.81029518}};
    if (!local.ensureSearchCircleNearSection()
        || !near(gis.circleX - 510261.375, local.circleX)
        || !near(gis.circleY - 2262.5690375, local.circleY)
        || !near(gis.radius, local.radius)) return 3;
    auto fixed = repaired;
    fixed.mode = 0;
    fixed.circleX = 5; fixed.circleY = 12; fixed.radius = 12.8;
    if (fixed.ensureSearchCircleNearSection() || fixed.circleX != 5
        || fixed.circleY != 12 || fixed.radius != 12.8) return 4;
    SlopeStandardDatModel example;
    example.nodes = {{1, 0, 0}, {2, 10, 8}};
    if (example.ensureSearchCircleNearSection() || example.circleX != 5
        || example.circleY != 12 || example.radius != 12.8) return 5;

    // Optional real DAT round trip for backend regression runs.
    if (app.arguments().size() == 3)
    {
        QFile input(app.arguments().at(1));
        QString error;
        SlopeStandardDatModel model;
        if (!input.open(QIODevice::ReadOnly)
            || !SlopeStandardDatModel::parse(QString::fromLocal8Bit(input.readAll()), &model, &error)
            || !model.validate(&error))
        {
            std::cerr << error.toStdString();
            return 6;
        }
        model.ensureSearchCircleNearSection();
        QFile output(app.arguments().at(2));
        if (!output.open(QIODevice::WriteOnly)) return 7;
        const QByteArray data = model.serialize().toLocal8Bit() + "\r\n";
        if (output.write(data) != data.size()) return 8;
    }
    std::cout << "PASS: GIS search seed, translation, retained seed and fixed circle\n";
    return 0;
}
