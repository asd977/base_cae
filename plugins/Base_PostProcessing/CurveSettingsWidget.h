#pragma once

#include <BasePage.h>

class QLabel;
class QPushButton;
class QPlainTextEdit;
class VisualWidgetService;

class CurveSettingsWidget : public BasePage
{
    Q_OBJECT
  public:
    explicit CurveSettingsWidget(VisualWidgetService* visualService, QWidget* parent = nullptr);
    ~CurveSettingsWidget() override = default;

    void beforeLoad() override;
    void beforeUnload() override;

    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;

  private slots:
    void reloadAndShowCurve();
    void switchBackTo3d();

  private:
    QString resolveDefaultTracePath() const;
    bool printTraceFile(const QString& filePath, QString* errMsg = nullptr);

  private:
    VisualWidgetService* m_visualService = nullptr;
    QPushButton* m_reloadBtn = nullptr;
    QPushButton* m_backTo3dBtn = nullptr;
    QLabel* m_statusLabel = nullptr;
    QPlainTextEdit* m_logEdit = nullptr;
    QString m_tracePath;
};
