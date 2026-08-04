#pragma once

#include <BasePage.h>

#include <QHash>
#include <QVector>
#include <array>
#include <vtkType.h>

class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class vtkUnstructuredGrid;

struct ReinforcementFaceSample
{
    std::array<double, 3> center{{0.0, 0.0, 0.0}};
    int layer = -1;
};

class CaveReinforcementFieldPage : public BasePage
{
    Q_OBJECT

  public:
    enum class Mode
    {
        Disabled = 0,
        ExistingBars = 1,
        Conceptual = 2
    };

    explicit CaveReinforcementFieldPage(QWidget* parent = nullptr);

    void beforeLoad() override;
    void beforeUnload() override;
    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;

    Mode mode() const;
    void setMode(Mode mode);
    bool exportFieldFiles(const QString& inputDirectory,
                          vtkUnstructuredGrid* grid,
                          int targetDimension,
                          const QHash<vtkIdType, int>& effectiveCellLayers,
                          const QVector<ReinforcementFaceSample>& excavationFaces,
                          QString* errorMessage = nullptr) const;

  private:
    void updateModeUi();

    QComboBox* m_mode = nullptr;
    QComboBox* m_formula = nullptr;
    QDoubleSpinBox* m_actualStiffness = nullptr;
    QDoubleSpinBox* m_indraratnaStiffnessCorrection = nullptr;
    QDoubleSpinBox* m_barLength = nullptr;
    QDoubleSpinBox* m_barDeltaC = nullptr;
    QDoubleSpinBox* m_cableLength = nullptr;
    QDoubleSpinBox* m_cableDeltaC = nullptr;
    QDoubleSpinBox* m_conceptStiffness = nullptr;
    QGroupBox* m_actualBox = nullptr;
    QGroupBox* m_conceptBox = nullptr;
};
