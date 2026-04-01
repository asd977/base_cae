#ifndef GEOSTRESSREGRESSIONWIDGET_H
#define GEOSTRESSREGRESSIONWIDGET_H

#include <BasePage.h>

class QWidget;

namespace Ui
{
class GeoStressRegressionWidget;
}

class GeoStressRegressionWidget : public BasePage
{
    Q_OBJECT

  public:
    explicit GeoStressRegressionWidget(QWidget* parent = nullptr);
    ~GeoStressRegressionWidget() override;

    void beforeLoad() override;
    void beforeUnload() override;

    bool exportIGEOSDatFile() const;

    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;

  signals:
    void computeRequested();

  private slots:
    void onComputeClicked();

  private:
    Ui::GeoStressRegressionWidget* ui;
};

#endif // GEOSTRESSREGRESSIONWIDGET_H
