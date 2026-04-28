#pragma once

#include <BasePage.h>
#include <PickManager.h>

class QLineEdit;
class QPushButton;

class CaveBlockPlanePage : public BasePage
{
    Q_OBJECT
  public:
    explicit CaveBlockPlanePage(QWidget* parent = nullptr);

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
    QLineEdit* m_x1Edit = nullptr;
    QLineEdit* m_y1Edit = nullptr;
    QLineEdit* m_z1Edit = nullptr;
    QLineEdit* m_x2Edit = nullptr;
    QLineEdit* m_y2Edit = nullptr;
    QLineEdit* m_z2Edit = nullptr;
    QLineEdit* m_x3Edit = nullptr;
    QLineEdit* m_y3Edit = nullptr;
    QLineEdit* m_z3Edit = nullptr;

    QLineEdit* m_fpEdit = nullptr;
    QLineEdit* m_cEdit = nullptr;
    QLineEdit* m_rtEdit = nullptr;

    QPushButton* m_pickFillBtn = nullptr;
};
