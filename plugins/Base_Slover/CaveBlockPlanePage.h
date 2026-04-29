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

    QString icutLine(int ii) const;
    QString paraLine(int ii) const;
    QVector3D p1Vector() const;
    QVector3D normalVector() const;
    double pickedFaceArea() const;

  private slots:
    void onSelectionSyncTick();
    void onFillFromPickedFace(vtkIdType faceKey);

  private:
    QString valueOrDefault(QLineEdit* edit, const QString& def) const;
    bool fillFromFaceKey(vtkIdType faceKey);

  private:
    Ui::CaveBlockPlanePage* ui = nullptr;
    QTimer* m_syncTimer = nullptr;
    vtkIdType m_lastFaceKey = -1;
};
