#pragma once

#include <BasePage.h>

#include <QJsonObject>
#include <QPair>
#include <QString>
#include <QVector>

namespace Ui
{
class SeepageBoundaryPage;
}

class SeepageBoundaryPage : public BasePage
{
    Q_OBJECT

  public:
    enum class HydraulicRole
    {
        None,
        Upstream,
        Downstream
    };

    enum class BoundaryConditionType
    {
        WaterLevel = 0,
        Flux = 1
    };

    explicit SeepageBoundaryPage(const QString& pageType, const QString& boundaryTitle,
                                 const QString& exportFileName, QWidget* parent = nullptr,
                                 bool removeFileWhenEmpty = false);
    ~SeepageBoundaryPage() override;

    void beforeLoad() override;
    void beforeUnload() override;

    bool exportDatFile();
    int selectionCount();
    QVector<int> selectedNodeIds();

    HydraulicRole hydraulicRole() const;
    bool hasHydraulicParameters() const;
    bool hasHydraulicState() const;
    BoundaryConditionType boundaryConditionType() const;
    bool usesWaterLevel() const;
    bool usesFlux() const;
    double waterLevel() const;
    double flux() const;
    double effectiveFlux() const;
    QVector<QPair<double, double>> waterLevelCurve() const;
    void setLegacyHydraulicState(double level, double flux,
                                 const QVector<QPair<double, double>>& curve);
    void setTransientCurveEnabled(bool enabled, double startTime, double endTime);
    bool validateHydraulicInputs(bool transient, double startTime, double endTime,
                                 QString* errorMessage);

    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;

  signals:
    void selectionCountChanged(int count);
    void hydraulicParametersChanged();

  private slots:
    void onClearClicked();
    void onDimChanged(int index);
    void onGroupChanged(int index);
    void onAddWaterLevelPointClicked();
    void onRemoveWaterLevelPointClicked();
    void onResetWaterLevelCurveClicked();
    void onPointPickToggled(bool enabled);
    void onBoundaryConditionTypeChanged(int index);

  private:
    void setupBatchGroupCombo();
    void refreshGroupCombo();
    void updateSelectionSummary();
    void configureHydraulicUi();
    void updateBoundaryConditionUi();
    void updateHydraulicSummary();
    void refreshWaterLevelCurveTable();
    void appendWaterLevelCurveRow(double time, double level);
    void syncWaterLevelCurveFromTable();
    void updateWaterLevelCurveHint();
    QVector<QPair<double, double>> defaultWaterLevelCurve() const;
    void updateWaterLevelSliderRange();
    void updateWaterLevelPreview();
    void clearWaterLevelPreview();
    bool selectedPointBounds(double bounds[6]);
    QString hydraulicRoleName() const;
    QString pickDomain();
    QString fluxFaceFileName() const;

  private:
    Ui::SeepageBoundaryPage* ui = nullptr;
    QString m_boundaryTitle;
    QString m_exportFileName;
    bool m_removeFileWhenEmpty = false;
    HydraulicRole m_hydraulicRole = HydraulicRole::None;
    QVector<QPair<double, double>> m_waterLevelCurve;
    bool m_hasHydraulicState = false;
    bool m_transientCurveEnabled = false;
    double m_transientStart = 0.0;
    double m_transientEnd = 0.0;
    bool m_pageActive = false;
    double m_sliderMinLevel = 0.0;
    double m_sliderMaxLevel = 1.0;
    bool m_updatingCurveTable = false;
};
