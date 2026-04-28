#pragma once

#include <BasePage.h>
#include <PickManager.h>

class QLineEdit;

namespace Ui
{
class CaveBlockPlanePage;
}

class CaveBlockPlanePage : public BasePage
{
    Q_OBJECT
  public:
    explicit CaveBlockPlanePage(QWidget* parent = nullptr);
    ~CaveBlockPlanePage() override;

    void beforeLoad() override;
    void beforeUnload() override;

    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;

    QString icutLine(int ii) const;
    QString paraLine(int ii) const;

  private slots:
    void onFillFromPickedFace();

  private:
    QString valueOrDefault(QLineEdit* edit, const QString& def) const;

  private:
    Ui::CaveBlockPlanePage* ui = nullptr;
};
