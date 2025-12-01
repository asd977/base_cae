#ifndef SOLVER_H
#define SOLVER_H

#include "CurveWidget.h"
#include "mfem.hpp"
#include <QDir>
#include <QVector>
#include <QMap>
#include <QStringList>
#include <interfaces/services/SolverService.h>

struct CurrentDistributionConfig
{
    QString meshFile;
    QString outputDir;
    QVector<int> electrolyteVolumes;
    QVector<int> electrodeFaces;
    QVector<int> insulationFaces;
    double conductivity = 1.0;
    double anodePotential = -0.44;
    double cathodePotential = 0.34;
    double exchangeCurrentDensity = 10.0; // A/m^2, secondary
    double anodicCoeff = 0.5;             // Tafel/transfer系数
    double cathodicCoeff = 0.5;
    double diffusivity = 1e-9;            // m^2/s, tertiary
    double bulkConcentration = 1.0;       // mol/m^3
};

class Solver : public SolverService
{
public:
    Solver();
    void test();

    void newProject(QString workPath) override;
    QStringList getValueStrings() override;
    void setValueStrings(QStringList values) override;
    void runCurrentDistribution(const QString &mode) override;

private:
    void loadCurrentConfigs(const QStringList &values);
    void runPrimaryCurrentDistribution();
    void runSecondaryCurrentDistribution();
    void runTertiaryCurrentDistribution();
    void solveCurrentDistribution(const QString &prefix, const CurrentDistributionConfig &cfg,
                                  bool exportOverpotential, bool exportConcentration);

    CurrentDistributionConfig &configFor(const QString &mode);
    const CurrentDistributionConfig &configFor(const QString &mode) const;

    QString m_workPath;
    QString m_mode = "primary";
    QMap<QString, CurrentDistributionConfig> m_configs;
private:
    CurveWidget *m_curveWidget;

};

#endif // SOLVER_H
