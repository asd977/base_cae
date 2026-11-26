#pragma once

#include <BasePage.h>
#include <PickManager.h>

namespace Ui
{
class GeoStressOuterForcePage;
}

class GeoStressOuterForcePage : public BasePage
{
    Q_OBJECT
  public:
    explicit GeoStressOuterForcePage(QWidget* parent = nullptr);
    ~GeoStressOuterForcePage() override;

    void beforeLoad() override;
    void beforeUnload() override;

    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;

    QString forceFile() const;
    int forceEnabled() const;
    void setCalculationMode(int calcMode);
    bool exportRoofFile() const;
    bool exportForceFile() const;

  private slots:
    void onClearClicked();
    void onBatchAddClicked();
    void onGroupHovered(int row);

  private:
    QString pickDomain() const;
    PickMode currentPickMode() const;
    void refreshGroupCombo();

  private:
    Ui::GeoStressOuterForcePage* ui = nullptr;
};
