#pragma once

#include <BasePage.h>

#include <QJsonObject>

namespace Ui
{
class StressStrainWidget;
}

class StressStrainWidget : public BasePage
{
    Q_OBJECT
  public:
    explicit StressStrainWidget(QWidget* parent = nullptr);
    ~StressStrainWidget() override;

    void beforeLoad() override;
    void beforeUnload() override;

    bool exportIfillDatFile() const;

    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;

  signals:
    void computeRequested();

  private slots:
    void onComputeClicked();

  private:
    Ui::StressStrainWidget* ui;
};
