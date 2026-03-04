#ifndef SEEPAGEWIDGET_H
#define SEEPAGEWIDGET_H

#include <BasePage.h>

#include <QJsonObject>

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

  private:
    Ui::SeepageWidget* ui;
};

#endif // SEEPAGEWIDGET_H
