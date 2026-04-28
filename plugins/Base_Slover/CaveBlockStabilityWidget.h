#pragma once

#include <BasePage.h>

class CaveBlockPlanePage;
class ProjectNode;
class QLineEdit;

class CaveBlockStabilityWidget : public BasePage
{
    Q_OBJECT
  public:
    explicit CaveBlockStabilityWidget(QWidget* parent = nullptr);

    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;

    bool exportDatFiles(const QVector<CaveBlockPlanePage*>& planePages) const;

  private:
    QString quoteValue(const QString& value) const;
    QString valueOrDefault(QLineEdit* edit, const QString& def) const;

  private:
    QLineEdit* m_icutFileEdit = nullptr;
    QLineEdit* m_safeFileEdit = nullptr;
    QLineEdit* m_weightEdit = nullptr;

    QLineEdit* m_xMinEdit = nullptr;
    QLineEdit* m_xMaxEdit = nullptr;
    QLineEdit* m_yMinEdit = nullptr;
    QLineEdit* m_yMaxEdit = nullptr;
    QLineEdit* m_zMinEdit = nullptr;
    QLineEdit* m_zMaxEdit = nullptr;

    QLineEdit* m_fxEdit = nullptr;
    QLineEdit* m_fyEdit = nullptr;
    QLineEdit* m_fzEdit = nullptr;

    QLineEdit* m_ifDirEdit = nullptr;
    QLineEdit* m_axEdit = nullptr;
    QLineEdit* m_ayEdit = nullptr;
    QLineEdit* m_azEdit = nullptr;
    QLineEdit* m_bxEdit = nullptr;
    QLineEdit* m_byEdit = nullptr;
    QLineEdit* m_bzEdit = nullptr;

    QLineEdit* m_im1Edit = nullptr;
    QLineEdit* m_im2Edit = nullptr;
};
