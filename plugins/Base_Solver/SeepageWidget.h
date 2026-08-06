#ifndef SEEPAGEWIDGET_H
#define SEEPAGEWIDGET_H

#include <BasePage.h>

#include <QJsonObject>
#include <QPair>
#include <QString>
#include <QVector>

namespace Ui
{
class SeepageWidget;
}

class SeepageBoundaryPage;

class SeepageWidget : public BasePage
{
    Q_OBJECT

  public:
    explicit SeepageWidget(QWidget* parent = nullptr);
    ~SeepageWidget() override;

    void beforeLoad() override;
    void beforeUnload() override;

    void setBoundaryPages(SeepageBoundaryPage* upstream,
                          SeepageBoundaryPage* downstream);

    double upstreamWaterLevel() const;
    double downstreamWaterLevel() const;
    double upstreamFlux() const;
    double downstreamFlux() const;
    bool naturalFreeSurfaceEnabled() const;
    bool drainWellEnabled() const;
    int computeType() const;
    QString transientInitialStateDirectory(const QString& workspaceRoot = QString()) const;
    bool validateInputs(QString* errorMessage) const;

    bool exportIseepDatFile() const;

    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;

  signals:
    void computeRequested();
    void stopRequested();
    void requirementsChanged();

  private slots:
    void onComputeClicked();
    void setComputing(bool computing);

  private:
    bool exportHwDropDatFiles(const QString& seepCaseDirName) const;
    bool exportTraceDatFiles() const;
    void updateComputeTypeVisibility();
    void updateOptionalControls();
    void updateBoundarySummary();
    void syncBoundaryTransientSettings();

    Ui::SeepageWidget* ui;
    SeepageBoundaryPage* upstreamBoundaryPage_ = nullptr;
    SeepageBoundaryPage* downstreamBoundaryPage_ = nullptr;

    double legacyUpstreamWaterLevel_ = 0.0;
    double legacyDownstreamWaterLevel_ = 0.0;
    double legacyUpstreamFlux_ = 0.0;
    double legacyDownstreamFlux_ = 0.0;
    QVector<QPair<double, double>> legacyUpstreamCurve_;
    QVector<QPair<double, double>> legacyDownstreamCurve_;
    bool legacyHydraulicStateLoaded_ = false;
};

#endif // SEEPAGEWIDGET_H
