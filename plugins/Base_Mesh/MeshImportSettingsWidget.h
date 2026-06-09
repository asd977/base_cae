#pragma once

#include <BasePage.h>

#include <QString>

namespace Ui
{
class MeshImportSettingsWidget;
}

class MeshImportSettingsWidget : public BasePage
{
    Q_OBJECT
  public:
    explicit MeshImportSettingsWidget(QWidget* parent = nullptr);
    ~MeshImportSettingsWidget() override;

    int verticalAxis() const;
    void setVerticalAxis(int axis);
    void setSourceFileName(const QString& fileName);

    void beforeLoad() override;
    void beforeUnload() override;

  signals:
    void importRequested();
    void verticalAxisChanged(int axis);

  private:
    Ui::MeshImportSettingsWidget* ui = nullptr;
};
