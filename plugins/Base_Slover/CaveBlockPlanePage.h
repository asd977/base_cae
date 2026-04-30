#pragma once

#include <BasePage.h>
#include <PickManager.h>
#include <QVector3D>

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

    QString icutLine(int ii);
    QString paraLine(int ii);
    QVector3D p1Vector();
    QVector3D normalVector();
    double pickedFaceArea();

  private:
    QString valueOrDefault(QLineEdit* edit, QString def);
    PickMode currentPickMode();
    GridKind currentGridKind();

  private:
    Ui::CaveBlockPlanePage* ui = nullptr;
};
