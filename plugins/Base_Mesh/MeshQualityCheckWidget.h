#pragma once

#include <BasePage.h>

#include <array>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QGroupBox;
class QLabel;
class QPushButton;
class QSpinBox;

struct MeshQualityCheckOptions
{
    int checkCase = 0;
    int expectedObject = 6;
    bool repairObject = false;
    int repairVerticalAxis = 0;
    bool repairAttributes = false;
    double minimumNodeDistance = 0.01;
    bool replaceNearbyNodes = false;
    double boundaryTolerance = 0.05;
    bool useSpecifiedBounds = false;
    std::array<double, 6> bounds = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    double jacobianTolerance = 0.001;
    int jacobianStartElement = 1;
    double maximumAspectRatio = 10.0;
    bool applyCorrections = false;
};

class MeshQualityCheckWidget : public BasePage
{
    Q_OBJECT

  public:
    explicit MeshQualityCheckWidget(QWidget* parent = nullptr);

    MeshQualityCheckOptions options() const;
    void setOptions(const MeshQualityCheckOptions& options);
    void setMeshContext(int suggestedObject,
                        int verticalAxis,
                        qint64 cellCount,
                        int dimension,
                        const std::array<double, 6>& bounds,
                        double characteristicLength,
                        bool useSuggestedObject);
    void setRunning(bool running);
    void setStatus(const QString& text, bool isError = false);

    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& object) override;
    void beforeLoad() override;
    void beforeUnload() override;

  signals:
    void checkRequested();
    void stopRequested();

  private:
    void updateVisibleRows();
    void updateAutomaticParameters();

    QComboBox* m_checkCase = nullptr;
    QComboBox* m_expectedObject = nullptr;
    QComboBox* m_objectHandling = nullptr;
    QComboBox* m_verticalHandling = nullptr;
    QComboBox* m_attributeHandling = nullptr;
    QComboBox* m_nearbyHandling = nullptr;
    QComboBox* m_boundaryHandling = nullptr;
    QCheckBox* m_applyCorrections = nullptr;
    QDoubleSpinBox* m_minimumDistance = nullptr;
    QDoubleSpinBox* m_boundaryTolerance = nullptr;
    std::array<QDoubleSpinBox*, 6> m_boundSpins = {};
    QDoubleSpinBox* m_jacobianTolerance = nullptr;
    QSpinBox* m_jacobianStart = nullptr;
    QDoubleSpinBox* m_aspectRatio = nullptr;
    QGroupBox* m_handlingGroup = nullptr;
    QGroupBox* m_parameterGroup = nullptr;
    QFormLayout* m_handlingForm = nullptr;
    QFormLayout* m_parameterForm = nullptr;
    QLabel* m_meshSummary = nullptr;
    QLabel* m_status = nullptr;
    QPushButton* m_startButton = nullptr;

    std::array<double, 6> m_meshBounds = {};
    double m_characteristicLength = 1.0;
    int m_meshDimension = 3;
    int m_cellCount = 1;
    bool m_running = false;
};
