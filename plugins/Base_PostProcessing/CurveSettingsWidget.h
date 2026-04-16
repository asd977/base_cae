#pragma once

#include <BasePage.h>

class VisualWidgetService;

namespace Ui
{
class CurveSettingsWidget;
}

class CurveSettingsWidget : public BasePage
{
    Q_OBJECT
  public:
    explicit CurveSettingsWidget(VisualWidgetService* visualService, QWidget* parent = nullptr);
    ~CurveSettingsWidget() override;

    void beforeLoad() override;
    void beforeUnload() override;

    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;

  private slots:
    void reloadAndShowCurve();
    void onResultDirChanged();

  private:
    void initResultDirectoryOptions();
    void refreshFilesForCurrentDir();
    QString selectedResultDirectoryPath() const;
    QString selectedTraceFilePath() const;
    QStringList selectedYFields() const;

  private:
    Ui::CurveSettingsWidget* ui = nullptr;
    VisualWidgetService* m_visualService = nullptr;
    bool m_initialized = false;
};
