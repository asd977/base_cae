#include "Solver.h"

#include <services/UiFrameService.h>
#include <ServiceRegistry.h>
#include "mfem.hpp"
#include <iostream>
#include <fstream>
#include <QCoreApplication>
#include <PublicConstants.h>
#include <PublicVariables.h>

using namespace std;

Solver::Solver()
{
    ServiceRegistry::instance().registerService<SolverService>(this);
    m_curveWidget = new CurveWidget(this);

    UiFrameService * uiFrameService = ServiceRegistry::instance().getService<UiFrameService>();
    uiFrameService->addWidget(m_curveWidget,"曲线");

    // test();
}

void Solver::test()
{
    runCurrentDistribution(m_mode);
}

void Solver::newProject(QString workPath)
{
    m_workPath = workPath;
    if (!m_workPath.isEmpty() && !m_workPath.endsWith(QDir::separator()))
    {
        m_workPath += QDir::separator();
    }

    const QString defaultMesh = QCoreApplication::applicationDirPath() + "/../tempGrid/mesh.msh";

    auto initConfig = [&](const QString &mode)
    {
        CurrentDistributionConfig cfg;
        cfg.meshFile = defaultMesh;
        cfg.outputDir = m_workPath;
        m_configs.insert(mode, cfg);
    };

    m_configs.clear();
    initConfig("primary");
    initConfig("secondary");
    initConfig("tertiary");
}

QStringList Solver::getValueStrings()
{
    QStringList values;
    values << "mode:" + m_mode;

    auto appendList = [&values](const QString &key, const QVector<int> &vec)
    {
        QStringList parts;
        for (int v : vec)
        {
            parts << QString::number(v);
        }
        values << key + ":" + parts.join(',');
    };

    for (auto it = m_configs.constBegin(); it != m_configs.constEnd(); ++it)
    {
        const auto &cfg = it.value();
        const QString prefix = it.key() + ".";

        appendList(prefix + "electrolyte", cfg.electrolyteVolumes);
        appendList(prefix + "electrode_faces", cfg.electrodeFaces);
        appendList(prefix + "insulation_faces", cfg.insulationFaces);

        if (!cfg.meshFile.isEmpty())
        {
            values << prefix + "mesh:" + cfg.meshFile;
        }

        if (!cfg.outputDir.isEmpty())
        {
            values << prefix + "output_dir:" + cfg.outputDir;
        }

        values << prefix + "conductivity:" + QString::number(cfg.conductivity);
        values << prefix + "anode_potential:" + QString::number(cfg.anodePotential);
        values << prefix + "cathode_potential:" + QString::number(cfg.cathodePotential);
        values << prefix + "exchange_current_density:" + QString::number(cfg.exchangeCurrentDensity);
        values << prefix + "anodic_coeff:" + QString::number(cfg.anodicCoeff);
        values << prefix + "cathodic_coeff:" + QString::number(cfg.cathodicCoeff);
        values << prefix + "diffusivity:" + QString::number(cfg.diffusivity);
        values << prefix + "bulk_concentration:" + QString::number(cfg.bulkConcentration);
    }

    return values;
}

void Solver::setValueStrings(QStringList values)
{
    loadCurrentConfigs(values);
}

CurrentDistributionConfig &Solver::configFor(const QString &mode)
{
    if (!m_configs.contains(mode))
    {
        CurrentDistributionConfig cfg;
        cfg.meshFile = QCoreApplication::applicationDirPath() + "/../tempGrid/mesh.msh";
        cfg.outputDir = m_workPath;
        m_configs.insert(mode, cfg);
    }
    return m_configs[mode];
}

const CurrentDistributionConfig &Solver::configFor(const QString &mode) const
{
    return m_configs[mode];
}

void Solver::loadCurrentConfigs(const QStringList &values)
{
    auto parseList = [](const QString &value) -> QVector<int>
    {
        QVector<int> result;
        const auto parts = value.split(',', Qt::SkipEmptyParts);
        for (const auto &p : parts)
        {
            bool ok = false;
            int v = p.toInt(&ok);
            if (ok)
            {
                result.append(v);
            }
        }
        return result;
    };

    for (const auto &item : values)
    {
        if (item.startsWith("mode:"))
        {
            m_mode = item.mid(QString("mode:").size());
            continue;
        }

        const int dotPos = item.indexOf('.');
        QString mode = m_mode;
        QString content = item;
        if (dotPos > 0)
        {
            mode = item.left(dotPos);
            content = item.mid(dotPos + 1);
        }

        CurrentDistributionConfig &cfg = configFor(mode);

        if (content.startsWith("mesh:"))
        {
            cfg.meshFile = content.mid(QString("mesh:").size());
        }
        else if (content.startsWith("electrolyte:"))
        {
            cfg.electrolyteVolumes = parseList(content.mid(QString("electrolyte:").size()));
        }
        else if (content.startsWith("electrode_faces:"))
        {
            cfg.electrodeFaces = parseList(content.mid(QString("electrode_faces:").size()));
        }
        else if (content.startsWith("insulation_faces:"))
        {
            cfg.insulationFaces = parseList(content.mid(QString("insulation_faces:").size()));
        }
        else if (content.startsWith("output_dir:"))
        {
            cfg.outputDir = content.mid(QString("output_dir:").size());
        }
        else if (content.startsWith("conductivity:"))
        {
            cfg.conductivity = content.mid(QString("conductivity:").size()).toDouble();
        }
        else if (content.startsWith("anode_potential:"))
        {
            cfg.anodePotential = content.mid(QString("anode_potential:").size()).toDouble();
        }
        else if (content.startsWith("cathode_potential:"))
        {
            cfg.cathodePotential = content.mid(QString("cathode_potential:").size()).toDouble();
        }
        else if (content.startsWith("exchange_current_density:"))
        {
            cfg.exchangeCurrentDensity = content.mid(QString("exchange_current_density:").size()).toDouble();
        }
        else if (content.startsWith("anodic_coeff:"))
        {
            cfg.anodicCoeff = content.mid(QString("anodic_coeff:").size()).toDouble();
        }
        else if (content.startsWith("cathodic_coeff:"))
        {
            cfg.cathodicCoeff = content.mid(QString("cathodic_coeff:").size()).toDouble();
        }
        else if (content.startsWith("diffusivity:"))
        {
            cfg.diffusivity = content.mid(QString("diffusivity:").size()).toDouble();
        }
        else if (content.startsWith("bulk_concentration:"))
        {
            cfg.bulkConcentration = content.mid(QString("bulk_concentration:").size()).toDouble();
        }
    }
}

void Solver::runCurrentDistribution(const QString &mode)
{
    if (!mode.isEmpty())
    {
        m_mode = mode;
    }

    if (m_mode == "secondary")
    {
        runSecondaryCurrentDistribution();
        return;
    }
    else if (m_mode == "tertiary")
    {
        runTertiaryCurrentDistribution();
        return;
    }

    runPrimaryCurrentDistribution();
}

void Solver::runPrimaryCurrentDistribution()
{
    solveCurrentDistribution("primary_current_distribution", configFor("primary"), false, false);
}

void Solver::runSecondaryCurrentDistribution()
{
    solveCurrentDistribution("secondary_current_distribution", configFor("secondary"), true, false);
}

void Solver::runTertiaryCurrentDistribution()
{
    solveCurrentDistribution("tertiary_current_distribution", configFor("tertiary"), true, true);
}

void Solver::solveCurrentDistribution(const QString &prefix, const CurrentDistributionConfig &cfg,
                                      bool exportOverpotential, bool exportConcentration)
{
    const QString meshPath = !cfg.meshFile.isEmpty() ? cfg.meshFile
                                                   : QCoreApplication::applicationDirPath() + "/../tempGrid/mesh.msh";

    mfem::Mesh mesh(meshPath.toStdString().c_str(), 1, 1);
    int dim = mesh.Dimension();

    cout << "Read mesh from: " << meshPath.toStdString() << endl;
    cout << "Dimension:           " << dim << endl;
    cout << "Elements (cells):    " << mesh.GetNE() << endl;
    cout << "Vertices (nodes):    " << mesh.GetNV() << endl;
    cout << "Boundary elements:   " << mesh.GetNBE() << endl;

    int order = 1;
    mfem::H1_FECollection fec(order, dim);
    mfem::FiniteElementSpace fes(&mesh, &fec, 1);

    mfem::GridFunction u(&fes);
    u = 0.0;

    int max_attr = mesh.attributes.Max();
    mfem::Vector sigma_vals(max_attr);
    sigma_vals = 0.0;

    for (int attr = 1; attr <= max_attr; ++attr)
    {
        if (cfg.electrolyteVolumes.contains(attr))
        {
            sigma_vals[attr - 1] = cfg.conductivity;
        }
    }

    mfem::PWConstCoefficient sigma_pc(sigma_vals);

    mfem::BilinearForm a(&fes);
    a.AddDomainIntegrator(new mfem::DiffusionIntegrator(sigma_pc));
    a.Assemble();

    mfem::LinearForm b(&fes);
    b = 0.0;
    b.Assemble();

    mfem::Array<int> ess_bdr(mesh.bdr_attributes.Max());
    ess_bdr = 0;

    mfem::Array<int> bdr_marker(mesh.bdr_attributes.Max());
    bdr_marker = 0;

    if (!cfg.electrodeFaces.isEmpty())
    {
        const double potentials[2] = { cfg.anodePotential, cfg.cathodePotential };
        for (int i = 0; i < cfg.electrodeFaces.size() && i < 2; ++i)
        {
            const int faceAttr = cfg.electrodeFaces[i];
            if (faceAttr >= 1 && faceAttr <= ess_bdr.Size())
            {
                ess_bdr[faceAttr - 1] = 1;
                bdr_marker = 0;
                bdr_marker[faceAttr - 1] = 1;
                mfem::ConstantCoefficient u_val(potentials[i]);
                u.ProjectBdrCoefficient(u_val, bdr_marker);
            }
        }
    }

    if (!cfg.insulationFaces.isEmpty())
    {
        cout << "Insulation faces registered: ";
        for (int f : cfg.insulationFaces)
        {
            cout << f << " ";
        }
        cout << endl;
    }

    mfem::Array<int> ess_tdof_list;
    fes.GetEssentialTrueDofs(ess_bdr, ess_tdof_list);

    mfem::SparseMatrix A;
    mfem::Vector B, X;
    a.FormLinearSystem(ess_tdof_list, u, b, A, X, B);

    cout << "Linear system size:  " << A.Height() << endl;

    mfem::GSSmoother M(A);
    PCG(A, M, B, X, 1, 500, 1e-12, 0.0);

    a.RecoverFEMSolution(X, b, u);

    double u_min = u.Min();
    double u_max = u.Max();
    cout << "u_min = " << u_min << ", u_max = " << u_max << endl;

    mfem::GradientGridFunctionCoefficient grad_u_coef(&u);
    mfem::FiniteElementSpace fes_vec(&mesh, &fec, dim);

    mfem::GridFunction grad_u(&fes_vec);
    grad_u = 0.0;
    grad_u.ProjectCoefficient(grad_u_coef);

    mfem::ScalarVectorProductCoefficient J_coef(sigma_pc, grad_u_coef);

    mfem::GridFunction J(&fes_vec);
    J = 0.0;
    J.ProjectCoefficient(J_coef);

    mfem::GridFunction overpotential(&fes);
    overpotential = 0.0;
    if (exportOverpotential)
    {
        overpotential = u;
        const double refPotential = (cfg.anodePotential + cfg.cathodePotential) * 0.5;
        overpotential -= refPotential;
    }

    mfem::GridFunction concentration(&fes);
    concentration = cfg.bulkConcentration;
    if (exportConcentration)
    {
        // 简单地将浓度与电位耦合，提供可视化参考
        concentration.Add(-cfg.diffusivity, u);
    }

    QString outputPrefix = cfg.outputDir;
    if (outputPrefix.isEmpty())
    {
        outputPrefix = m_workPath.isEmpty() ? QCoreApplication::applicationDirPath() + QDir::separator()
                                      : m_workPath;
    }

    mfem::ParaViewDataCollection dc(prefix.toStdString(), &mesh);
    dc.SetPrefixPath(outputPrefix.toStdString());
    dc.SetTime(0.0);
    dc.SetCycle(0);

    dc.RegisterField("potential", &u);
    dc.RegisterField("grad_u", &grad_u);
    dc.RegisterField("current_density", &J);
    if (exportOverpotential)
    {
        dc.RegisterField("overpotential", &overpotential);
    }
    if (exportConcentration)
    {
        dc.RegisterField("concentration", &concentration);
    }

    dc.Save();

    cout << prefix.toStdString() << " exported to " << outputPrefix.toStdString() << endl;
}
