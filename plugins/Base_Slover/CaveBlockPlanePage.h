#pragma once

#include <BasePage.h>
#include <PickManager.h>
#include <QVector3D>

class QLineEdit;
class QTimer;

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

  private slots:
    void onSelectionSyncTick();
    void onFillFromPickedFace(vtkIdType faceKey);

  private:
    QString valueOrDefault(QLineEdit* edit, QString def);
    bool fillFromFaceKey(vtkIdType faceKey);

  private:
    Ui::CaveBlockPlanePage* ui = nullptr;
    QTimer* m_syncTimer = nullptr;
    vtkIdType m_lastFaceKey = -1;
};
