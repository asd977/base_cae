#pragma once

#include <BasePage.h>

class CaveBlockPlanePage;
class QLineEdit;

namespace Ui
{
class CaveBlockStabilityWidget;
}

class CaveBlockStabilityWidget : public BasePage
{
    Q_OBJECT
  public:
    explicit CaveBlockStabilityWidget(QWidget* parent = nullptr);
    ~CaveBlockStabilityWidget() override;

    void beforeLoad() override;
    void beforeUnload() override;

    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;

    bool exportDatFiles(QVector<CaveBlockPlanePage*>& planePages);
  signals:
    void computeRequested();
    void stopRequested();

  public slots:
    void setComputing(bool computing);

  private:
    QString quoteValue(QString value);
    QString valueOrDefault(QLineEdit* edit, QString def);

  private:
    Ui::CaveBlockStabilityWidget* ui = nullptr;
};
