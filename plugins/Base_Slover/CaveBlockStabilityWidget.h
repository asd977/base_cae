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

    bool exportDatFiles(const QVector<CaveBlockPlanePage*>& planePages) const;
  signals:
    void computeRequested();

  private:
    QString quoteValue(const QString& value) const;
    QString valueOrDefault(QLineEdit* edit, const QString& def) const;

  private:
    Ui::CaveBlockStabilityWidget* ui = nullptr;
};
