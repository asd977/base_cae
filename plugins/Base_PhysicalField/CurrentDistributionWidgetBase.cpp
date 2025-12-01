#include "CurrentDistributionWidgetBase.h"

#include <ServiceRegistry.h>
#include <services/SolverService.h>
#include <mesh/BoundaryConditions.h>
#include <QFileDialog>
#include <QStringList>

CurrentDistributionWidgetBase::CurrentDistributionWidgetBase(const QString &pageName, const QString &mode, QWidget *parent)
    : BasePage(pageName, parent),
      m_mode(mode)
{
}

void CurrentDistributionWidgetBase::setNode(ProjectNode *node)
{
    m_node = node;
}

void CurrentDistributionWidgetBase::beforeLoad()
{
    loadConfigFromSolver();
}

void CurrentDistributionWidgetBase::beforeUnload()
{
    syncConfigToSolver();
}

void CurrentDistributionWidgetBase::connectCommonUi()
{
    if (auto *btn = electrolyteButton())
    {
        connect(btn, &QPushButton::clicked, this, &CurrentDistributionWidgetBase::onElectrolyteClicked);
    }
    if (auto *btn = electrodeButton())
    {
        connect(btn, &QPushButton::clicked, this, &CurrentDistributionWidgetBase::onElectrodeClicked);
    }
    if (auto *btn = insulationButton())
    {
        connect(btn, &QPushButton::clicked, this, &CurrentDistributionWidgetBase::onInsulationClicked);
    }
    if (auto *btn = meshBrowseButton())
    {
        connect(btn, &QPushButton::clicked, this, &CurrentDistributionWidgetBase::onMeshBrowse);
    }
    if (auto *btn = outputBrowseButton())
    {
        connect(btn, &QPushButton::clicked, this, &CurrentDistributionWidgetBase::onOutputBrowse);
    }
    if (auto *btn = runButton())
    {
        connect(btn, &QPushButton::clicked, this, &CurrentDistributionWidgetBase::onRunClicked);
    }

    auto connectSpin = [this](QDoubleSpinBox *spin)
    {
        if (spin)
        {
            connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &CurrentDistributionWidgetBase::syncConfigToSolver);
        }
    };

    connectSpin(conductivitySpin());
    connectSpin(anodePotentialSpin());
    connectSpin(cathodePotentialSpin());
    connectSpin(exchangeCurrentSpin());
    connectSpin(anodicCoeffSpin());
    connectSpin(cathodicCoeffSpin());
    connectSpin(diffusivitySpin());
    connectSpin(concentrationSpin());

    if (auto *line = meshPathLineEdit())
    {
        connect(line, &QLineEdit::editingFinished, this, &CurrentDistributionWidgetBase::syncConfigToSolver);
    }
    if (auto *line = outputDirLineEdit())
    {
        connect(line, &QLineEdit::editingFinished, this, &CurrentDistributionWidgetBase::syncConfigToSolver);
    }
}

void CurrentDistributionWidgetBase::onElectrolyteClicked()
{
    cacheCurrentSelections(static_cast<int>(BCSelectMode::Volume), m_electrolyteVolumes);
}

void CurrentDistributionWidgetBase::onElectrodeClicked()
{
    cacheCurrentSelections(static_cast<int>(BCSelectMode::Face), m_electrodeFaces);
}

void CurrentDistributionWidgetBase::onInsulationClicked()
{
    cacheCurrentSelections(static_cast<int>(BCSelectMode::Face), m_insulationFaces);
}

void CurrentDistributionWidgetBase::onMeshBrowse()
{
    const QString file = QFileDialog::getOpenFileName(this, tr("选择网格"), meshPathLineEdit() ? meshPathLineEdit()->text() : QString(), tr("Mesh (*.msh)"));
    if (!file.isEmpty() && meshPathLineEdit())
    {
        meshPathLineEdit()->setText(file);
        syncConfigToSolver();
    }
}

void CurrentDistributionWidgetBase::onOutputBrowse()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("选择输出目录"), outputDirLineEdit() ? outputDirLineEdit()->text() : QString());
    if (!dir.isEmpty() && outputDirLineEdit())
    {
        outputDirLineEdit()->setText(dir);
        syncConfigToSolver();
    }
}

void CurrentDistributionWidgetBase::onRunClicked()
{
    syncConfigToSolver();
    auto *solverService = ServiceRegistry::instance().getService<SolverService>();
    if (solverService)
    {
        solverService->runCurrentDistribution(m_mode);
    }
}

void CurrentDistributionWidgetBase::cacheCurrentSelections(int dim, QVector<int> &store)
{
    store.clear();

    const auto selections = BoundaryConditions::instance().getAllSelections();
    for (const auto &item : selections)
    {
        if (item.dim == dim)
        {
            if (!store.contains(item.physId))
            {
                store.append(item.physId);
            }
        }
    }

    refreshSelectionDisplays();
    syncConfigToSolver();
}

void CurrentDistributionWidgetBase::syncConfigToSolver()
{
    auto *solverService = ServiceRegistry::instance().getService<SolverService>();
    if (!solverService)
        return;

    QStringList values;
    values << "mode:" + m_mode;

    auto vecToString = [](const QVector<int> &vec)
    {
        QStringList parts;
        for (int v : vec)
        {
            parts << QString::number(v);
        }
        return parts.join(',');
    };

    const QString prefix = m_mode + ".";
    values << prefix + "electrolyte:" + vecToString(m_electrolyteVolumes);
    values << prefix + "electrode_faces:" + vecToString(m_electrodeFaces);
    values << prefix + "insulation_faces:" + vecToString(m_insulationFaces);
    if (meshPathLineEdit())
    {
        values << prefix + "mesh:" + meshPathLineEdit()->text();
    }
    if (outputDirLineEdit())
    {
        values << prefix + "output_dir:" + outputDirLineEdit()->text();
    }

    auto appendSpin = [&values, &prefix](const QString &key, QDoubleSpinBox *spin)
    {
        if (spin)
        {
            values << prefix + key + ":" + QString::number(spin->value());
        }
    };

    appendSpin("conductivity", conductivitySpin());
    appendSpin("anode_potential", anodePotentialSpin());
    appendSpin("cathode_potential", cathodePotentialSpin());
    appendSpin("exchange_current_density", exchangeCurrentSpin());
    appendSpin("anodic_coeff", anodicCoeffSpin());
    appendSpin("cathodic_coeff", cathodicCoeffSpin());
    appendSpin("diffusivity", diffusivitySpin());
    appendSpin("bulk_concentration", concentrationSpin());

    solverService->setValueStrings(values);
}

void CurrentDistributionWidgetBase::loadConfigFromSolver()
{
    auto *solverService = ServiceRegistry::instance().getService<SolverService>();
    if (!solverService)
        return;

    const auto values = solverService->getValueStrings();

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
        const int dotPos = item.indexOf('.');
        QString itemMode = m_mode;
        QString content = item;
        if (dotPos > 0)
        {
            itemMode = item.left(dotPos);
            content = item.mid(dotPos + 1);
        }

        if (itemMode != m_mode)
        {
            continue;
        }

        if (content.startsWith("mesh:") && meshPathLineEdit())
        {
            meshPathLineEdit()->setText(content.mid(QString("mesh:").size()));
        }
        else if (content.startsWith("output_dir:") && outputDirLineEdit())
        {
            outputDirLineEdit()->setText(content.mid(QString("output_dir:").size()));
        }
        else if (content.startsWith("conductivity:") && conductivitySpin())
        {
            conductivitySpin()->setValue(content.mid(QString("conductivity:").size()).toDouble());
        }
        else if (content.startsWith("anode_potential:") && anodePotentialSpin())
        {
            anodePotentialSpin()->setValue(content.mid(QString("anode_potential:").size()).toDouble());
        }
        else if (content.startsWith("cathode_potential:") && cathodePotentialSpin())
        {
            cathodePotentialSpin()->setValue(content.mid(QString("cathode_potential:").size()).toDouble());
        }
        else if (content.startsWith("exchange_current_density:") && exchangeCurrentSpin())
        {
            exchangeCurrentSpin()->setValue(content.mid(QString("exchange_current_density:").size()).toDouble());
        }
        else if (content.startsWith("anodic_coeff:") && anodicCoeffSpin())
        {
            anodicCoeffSpin()->setValue(content.mid(QString("anodic_coeff:").size()).toDouble());
        }
        else if (content.startsWith("cathodic_coeff:") && cathodicCoeffSpin())
        {
            cathodicCoeffSpin()->setValue(content.mid(QString("cathodic_coeff:").size()).toDouble());
        }
        else if (content.startsWith("diffusivity:") && diffusivitySpin())
        {
            diffusivitySpin()->setValue(content.mid(QString("diffusivity:").size()).toDouble());
        }
        else if (content.startsWith("bulk_concentration:") && concentrationSpin())
        {
            concentrationSpin()->setValue(content.mid(QString("bulk_concentration:").size()).toDouble());
        }
        else if (content.startsWith("electrolyte:"))
        {
            m_electrolyteVolumes = parseList(content.mid(QString("electrolyte:").size()));
        }
        else if (content.startsWith("electrode_faces:"))
        {
            m_electrodeFaces = parseList(content.mid(QString("electrode_faces:").size()));
        }
        else if (content.startsWith("insulation_faces:"))
        {
            m_insulationFaces = parseList(content.mid(QString("insulation_faces:").size()));
        }
    }

    refreshSelectionDisplays();
}

void CurrentDistributionWidgetBase::refreshSelectionDisplays()
{
    auto vecToString = [](const QVector<int> &vec)
    {
        QStringList parts;
        for (int v : vec)
        {
            parts << QString::number(v);
        }
        return parts.join(',');
    };

    if (electrolyteLineEdit())
    {
        electrolyteLineEdit()->setText(vecToString(m_electrolyteVolumes));
    }
    if (electrodeLineEdit())
    {
        electrodeLineEdit()->setText(vecToString(m_electrodeFaces));
    }
    if (insulationLineEdit())
    {
        insulationLineEdit()->setText(vecToString(m_insulationFaces));
    }
}

