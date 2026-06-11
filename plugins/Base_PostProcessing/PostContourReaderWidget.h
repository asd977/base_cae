#pragma once

#include <BasePage.h>

#include <QColor>
#include <QMap>
#include <QString>
#include <QVector>

#include <vtkSmartPointer.h>

class VisualWidgetService;
class vtkActor;
class vtkActor2D;
class vtkPolyData;
class vtkPolyDataMapper;

namespace Ui
{
class PostContourReaderWidget;
}

class PostContourReaderWidget : public BasePage
{
    Q_OBJECT
  public:
    explicit PostContourReaderWidget(QWidget* parent = nullptr);
    ~PostContourReaderWidget() override;

    void beforeLoad() override;
    void beforeUnload() override;

  private slots:
    void onBrowseClicked();
    void onLoadClicked();
    void onClearClicked();
    void onTextVisibleChanged(bool checked);

  private:
    struct ScrPoint
    {
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
    };

    struct ScrLine
    {
        ScrPoint p1;
        ScrPoint p2;
        QString layer;
        int colorIndex = 7;
        int sourceLine = 0;
    };

    struct ScrText
    {
        ScrPoint p;
        QString text;
        QString layer;
        int colorIndex = 7;
        double height = 0.0;
        double angle = 0.0;
        int sourceLine = 0;
    };

    struct ScrModel
    {
        QString filePath;
        int totalLines = 0;
        int pointDefinitionCount = 0;
        int skippedLineCount = 0;
        QVector<ScrLine> lines;
        QVector<ScrText> texts;
        QMap<QString, int> layerLineCounts;
        QMap<QString, int> layerTextCounts;
        bool hasBounds = false;
        double minX = 0.0;
        double maxX = 0.0;
        double minY = 0.0;
        double maxY = 0.0;
        double minZ = 0.0;
        double maxZ = 0.0;
    };

    Ui::PostContourReaderWidget* ui = nullptr;
    VisualWidgetService* m_visualService = nullptr;

    vtkSmartPointer<vtkPolyData> m_linePolyData;
    vtkSmartPointer<vtkPolyDataMapper> m_lineMapper;
    vtkSmartPointer<vtkActor> m_lineActor;
    QVector<vtkSmartPointer<vtkActor2D>> m_textActors;

    ScrModel m_model;
    bool m_lineActorAttached = false;

    bool ensureVisualService();
    bool parseScrFile(const QString& filePath, ScrModel& model, QString* errorMessage) const;
    void displayModel(bool resetCamera = true);
    void buildLineActor();
    void buildTextActors();
    void removeActorsFromRenderer();
    void setStatusMessage(const QString& message);

    static QColor aciColor(int colorIndex);
    static void includePointInBounds(ScrModel& model, const ScrPoint& p);
};
