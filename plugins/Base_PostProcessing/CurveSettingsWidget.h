#pragma once

#include <BasePage.h>

class QCheckBox;
class QComboBox;
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
    void onResultDirChanged();

  private:
    void initResultDirectoryOptions();
    void refreshFilesForCurrentDir();
    QString selectedResultDirectoryPath() const;
    QString selectedTraceFilePath() const;
    bool printTraceFile(const QString& filePath, QString* errMsg = nullptr);
    QStringList selectedYFields() const;

  private:
    VisualWidgetService* m_visualService = nullptr;

    QComboBox* m_resultDirCombo = nullptr;
    QComboBox* m_fileCombo = nullptr;
    QComboBox* m_xFieldCombo = nullptr;
    QCheckBox* m_fosCheck = nullptr;
    QCheckBox* m_ptotCheck = nullptr;
    QCheckBox* m_maxUrCheck = nullptr;
    QCheckBox* m_loopCheck = nullptr;

    QPushButton* m_reloadBtn = nullptr;
    QPushButton* m_backTo3dBtn = nullptr;
    QLabel* m_statusLabel = nullptr;
    QPlainTextEdit* m_logEdit = nullptr;

    bool m_initialized = false;
};
