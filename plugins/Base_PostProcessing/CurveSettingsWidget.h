#pragma once

#include <BasePage.h>

#include <QString>
#include <QVector>

class QCustomPlot;
class QPushButton;
class QLabel;

class CurveSettingsWidget : public BasePage
{
    Q_OBJECT
  public:
    explicit CurveSettingsWidget(QWidget* parent = nullptr);
    ~CurveSettingsWidget() override = default;

    void beforeLoad() override;
    void beforeUnload() override;

    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;

  private slots:
    void reloadFromDefaultPath();

  private:
    struct TracePoint
    {
        int istep = 0;
        double fos = 0.0;
        double maxUr = 0.0;
    };

    bool loadTraceFile(const QString& filePath);
    void renderPlot();

  private:
    QCustomPlot* m_plot = nullptr;
    QPushButton* m_reloadBtn = nullptr;
    QLabel* m_statusLabel = nullptr;

    QString m_tracePath;
    QVector<TracePoint> m_points;
};
