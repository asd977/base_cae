#ifndef SLOPESTANDARDDATMODEL_H
#define SLOPESTANDARDDATMODEL_H

#include <QString>
#include <QVector>

struct SlopeStandardSegment
{
    int startNode{0};
    int endNode{0};
    int soilId{0};
};

struct SlopeStandardSoil
{
    int id{0};
    double frictionAngle{0.0};
    double cohesion{0.0};
    double naturalUnitWeight{0.0};
    double saturatedUnitWeight{0.0};
    int poreMethod{-1};
    double ru{0.0};
};

struct SlopeStandardNode
{
    int id{0};
    double x{0.0};
    double y{0.0};
};

struct SlopeStandardSurfaceLoad
{
    int segmentId{0};
    double force{0.0};
};

class SlopeStandardDatModel
{
  public:
    QString projectTitle;
    int mode{1};
    double searchStep{0.2};
    int method{2};
    double radius{12.8};
    double circleX{5.0};
    double circleY{12.0};
    double waterUnitWeight{9.81};
    int sliceCount{50};
    QVector<SlopeStandardSegment> segments;
    double outsideWaterLevel{9999.0};
    QVector<SlopeStandardSoil> soils;
    QVector<SlopeStandardNode> nodes;
    QVector<SlopeStandardSurfaceLoad> surfaceLoads;
    double slopeHeight{0.0};
    int earthquakeEnabled{0};
    int earthquakeIntensity{0};
    int earthquakeMode{0};
    int earthquakeDirectionH{0};
    int earthquakeDirectionV{0};
    QVector<SlopeStandardSegment> phreaticSegments;

    static bool parse(const QString& text, SlopeStandardDatModel* model,
                      QString* errorMessage = nullptr);
    QString serialize() const;
    bool validate(QString* errorMessage = nullptr) const;
    bool ensureSearchCircleNearSection();
    int poreMethod() const;
};

#endif // SLOPESTANDARDDATMODEL_H
