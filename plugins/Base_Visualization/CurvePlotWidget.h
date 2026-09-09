#pragma once

#include <QSet>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <vtkSmartPointer.h>

class QLabel;
class QToolButton;
class QVTKOpenGLNativeWidget;
class vtkChartXY;
class vtkContextView;
class vtkGenericOpenGLRenderWindow;
class vtkTable;

class CurvePlotWidget : public QWidget
{
    Q_OBJECT

  public:
    explicit CurvePlotWidget(QWidget* parent = nullptr);

    bool loadDataFromTraceFile(const QString& filePath, const QString& xField,
                               const QSet<QString>& yFields, QString* errMsg = nullptr);

  private slots:
    void resetChartView();
    void setNormalized(bool normalized);

  private:
    struct TracePoint
    {
        double istep = 0.0;
        double fos = 0.0;
        double ptot = 0.0;
        double maxUr = 0.0;
        double loop = 0.0;

        bool hasIstep = false;
        bool hasFos = false;
        bool hasPtot = false;
        bool hasMaxUr = false;
        bool hasLoop = false;
    };

    bool parseTraceLine(const QString& line, TracePoint& point) const;
    void renderPlot();
    void updateSummary();
    vtkSmartPointer<vtkTable> buildPlotTable(const QStringList& fields) const;
    QStringList normalizedFieldOrder(const QSet<QString>& fields) const;
    double fieldValue(const TracePoint& point, const QString& field, bool* valid = nullptr) const;
    QString fieldDisplayName(const QString& field) const;
    void updateStatus(const QString& text, bool error = false);

  private:
    QVTKOpenGLNativeWidget* m_vtkWidget = nullptr;
    QLabel* m_titleLabel = nullptr;
    QLabel* m_summaryLabel = nullptr;
    QLabel* m_statusLabel = nullptr;
    QToolButton* m_normalizeButton = nullptr;

    vtkSmartPointer<vtkGenericOpenGLRenderWindow> m_renderWindow;
    vtkSmartPointer<vtkContextView> m_contextView;
    vtkSmartPointer<vtkChartXY> m_chart;
    vtkSmartPointer<vtkTable> m_table;

    QString m_tracePath;
    QString m_xField = QStringLiteral("ISTEP");
    QStringList m_yFields;
    QVector<TracePoint> m_points;
    bool m_normalized = false;
    bool m_showLegend = false;
};
