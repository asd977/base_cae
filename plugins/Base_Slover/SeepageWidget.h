#ifndef SEEPAGEWIDGET_H
#define SEEPAGEWIDGET_H

#include <BasePage.h>

#include <QJsonObject>
#include <QVector>

namespace Ui
{
class SeepageWidget;
}

class SeepageWidget : public BasePage
{
    Q_OBJECT

  public:
    explicit SeepageWidget(QWidget* parent = nullptr);
    ~SeepageWidget() override;

    void beforeLoad() override;
    void beforeUnload() override;

    double upstreamWaterLevel() const;
    double downstreamWaterLevel() const;
    double upstreamFlux() const;
    double downstreamFlux() const;

    bool exportIseepDatFile() const;
    bool exportInouseDatFile() const;
    bool exportMaMaskDatFile() const;
    bool exportMaRockDatFile() const;
    bool exportKetraceDatFile() const;
    bool exportJdtraceDatFile() const;

    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;

  signals:
    void computeRequested();

  private slots:
    void onComputeClicked();
    void onEditGlobalMappingsClicked();
    void onEditHwDropCurvesClicked();

  private:
    struct MaterialMapping
    {
        int oldMaterial = 0;
        int newMaterial = 0;
    };

    struct WaterLevelPoint
    {
        double time = 0.0;
        double upHead = 0.0;
        double dwHead = 0.0;
    };

    bool exportHwDropDatFiles(const QString& seepCaseDirName) const;

    Ui::SeepageWidget* ui;
    QVector<MaterialMapping> globalMappings_;
    QVector<WaterLevelPoint> waterLevelPoints_;
};

#endif // SEEPAGEWIDGET_H
