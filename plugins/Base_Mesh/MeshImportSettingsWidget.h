#pragma once

#include <BasePage.h>

#include <QString>

class QComboBox;
class QLabel;
class QPushButton;

class MeshImportSettingsWidget : public BasePage
{
    Q_OBJECT
  public:
    explicit MeshImportSettingsWidget(QWidget* parent = nullptr);

    int verticalAxis() const;
    void setVerticalAxis(int axis);
    void setSourceFileName(const QString& fileName);

    void beforeLoad() override;
    void beforeUnload() override;

  signals:
    void importRequested();
    void verticalAxisChanged(int axis);

  private:
    QComboBox* m_comboVerticalAxis = nullptr;
    QLabel* m_labelSourceFile = nullptr;
    QPushButton* m_buttonImport = nullptr;
};
