#pragma once

#include <QWidget>

#include <QSet>
#include <QVector>

class QLabel;
class QCustomPlot;

class CurvePlotWidget : public QWidget
{
    Q_OBJECT
  public:
    explicit CurvePlotWidget(QWidget* parent = nullptr);

    bool loadDataFromTraceFile(const QString& filePath, const QString& xField,
                               const QSet<QString>& yFields, QString* errMsg = nullptr);

  private:
    struct TracePoint
    {
        int istep = 0;
        double fos = 0.0;
        double ptot = 0.0;
        double maxUr = 0.0;
        double loop = 0.0;
    };

    void renderPlot(const QString& xField, const QSet<QString>& yFields);

  private:
    QCustomPlot* m_plot = nullptr;
    QLabel* m_statusLabel = nullptr;
    QString m_tracePath;
    QVector<TracePoint> m_points;
};
