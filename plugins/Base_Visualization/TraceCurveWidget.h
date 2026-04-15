#pragma once

#include <QWidget>

#include <QVector>

class QLabel;
class QCustomPlot;

class TraceCurveWidget : public QWidget
{
    Q_OBJECT
  public:
    explicit TraceCurveWidget(QWidget* parent = nullptr);

    bool loadTraceFile(const QString& filePath, QString* errMsg = nullptr);
    QString currentTraceFile() const;

  private:
    struct TracePoint
    {
        int istep = 0;
        double fos = 0.0;
        double ptot = 0.0;
        double maxUr = 0.0;
        double loop = 0.0;
    };

    void renderPlot();

  private:
    QCustomPlot* m_plot = nullptr;
    QLabel* m_statusLabel = nullptr;
    QString m_tracePath;
    QVector<TracePoint> m_points;
};
