#pragma once

#include "SketchTypes.h"

#include <QString>
#include <QElapsedTimer>
#include <QTimer>
#include <functional>
#include <memory>
#include <utility>
#include <vector>
#include <vtkSmartPointer.h>

class TopoDS_Shape;
class vtkActor;
class vtkActor2D;
class vtkCallbackCommand;
class vtkCamera;
class vtkObject;
class vtkPoints;
class vtkPolyData;
class vtkTextActor;

namespace GeometrySketch
{

class SketchViewController
{
public:
    SketchViewController() = default;
    ~SketchViewController();

    SketchViewController(const SketchViewController&) = delete;
    SketchViewController& operator=(const SketchViewController&) = delete;

    void setObjectPrefix(const QString& uuid);
    void setPlaneSettings(const PlaneSettings& settings);

    void enter();
    void exit();
    bool isActive() const { return m_active; }

    void alignCameraToPlane();
    bool fitToShape(const TopoDS_Shape& shape, double margin = 1.25);
    bool fitToPoints(const std::vector<Point2D>& points, double margin = 1.25);
    void redrawGrid();
    void clearGrid(bool renderNow = true);

    void updatePreview(const TopoDS_Shape& shape);
    void updateConstructionPreview(const TopoDS_Shape& shape);
    void updateSelectionHighlight(const TopoDS_Shape& shape);
    void clearSelectionHighlight();
    void updateSelectedPointMarkers(const std::vector<Point2D>& points);
    void clearSelectedPointMarkers();
    void clearPreview();

    void updatePointMarkers(const std::vector<Point2D>& points);
    void updateRubberLine(bool visible, const Point2D& from, const Point2D& to);
    void updateRubberShape(bool visible, const std::vector<Point2D>& points, bool closed);
    void updateConstructionGuide(bool visible, const std::vector<std::pair<Point2D, Point2D>>& segments);
    void updateTangentGuideLine(bool visible, const Point2D& from, const Point2D& to);
    void updateSnapIndicator(bool visible, const Point2D& point);
    void updateCursorText(double u, double v, bool snapped);
    void updateHoverPointText(bool visible, const Point2D& point, const QString& text = QString());
    void updateFixedPointText(bool visible, const Point2D& point, const QString& text = QString());
    void setBoxSelectionEnabled(bool enabled);

    bool screenToUv(int x, int y, double& u, double& v) const;
    double screenToleranceUv(int pixels = 9) const;

    std::function<void(double u, double v, bool rightButton)> clickHandler;
    std::function<void(double u, double v)> moveHandler;
    std::function<void(double u0, double v0, double u1, double v1, bool crossing, bool additive)> boxSelectHandler;
    std::function<bool(double u, double v)> pointDragStartHandler;
    std::function<void(double u, double v)> pointDragMoveHandler;
    std::function<void(double u, double v)> pointDragEndHandler;

private:
    void setupObservers();
    void teardownObservers();
    void startResizeWatch();
    void stopResizeWatch();
    void checkRenderWindowSize(bool force = false);

    void onMousePress(vtkObject* caller, unsigned long eventId);
    void onMouseRelease(vtkObject* caller, unsigned long eventId);
    void onMouseMove(vtkObject* caller, unsigned long eventId);
    void updateSelectionBox(bool visible, const Point2D& first = {}, const Point2D& second = {}, bool crossing = false);
    void updatePointTextActor(vtkSmartPointer<vtkTextActor>& actor, const QString& suffix, bool visible,
                              const Point2D& point, const QString& text, bool fixed);

    QString key(const QString& suffix) const;

private:
    QString m_uuid;
    PlaneSettings m_settings;
    bool m_active = false;
    int m_pressX = 0;
    int m_pressY = 0;
    double m_pressU = 0.0;
    double m_pressV = 0.0;
    unsigned long m_pressButtonEvent = 0;
    bool m_boxSelectionEnabled = false;
    bool m_draggingSelectionBox = false;
    bool m_draggingPoint = false;
    int m_lastMoveX = -1;
    int m_lastMoveY = -1;
    QElapsedTimer m_renderClock;
    std::unique_ptr<QTimer> m_resizeTimer;
    int m_lastWindowWidth = -1;
    int m_lastWindowHeight = -1;
    bool m_redrawingGrid = false;
    bool m_previousUseFxaa = false;
    bool m_hasPreviousUseFxaa = false;

    unsigned long m_leftPressTag = 0;
    unsigned long m_rightPressTag = 0;
    unsigned long m_leftReleaseTag = 0;
    unsigned long m_rightReleaseTag = 0;
    unsigned long m_moveTag = 0;
    unsigned long m_wheelForwardTag = 0;
    unsigned long m_wheelBackwardTag = 0;
    unsigned long m_configureTag = 0;
    unsigned long m_cameraModifiedTag = 0;

    vtkSmartPointer<vtkCallbackCommand> m_pressCallback;
    vtkSmartPointer<vtkCallbackCommand> m_releaseCallback;
    vtkSmartPointer<vtkCallbackCommand> m_moveCallback;
    vtkSmartPointer<vtkCallbackCommand> m_viewCallback;
    vtkSmartPointer<vtkCamera> m_observedCamera;

    vtkSmartPointer<vtkActor> m_gridActor;
    vtkSmartPointer<vtkActor> m_uAxisActor;
    vtkSmartPointer<vtkActor> m_vAxisActor;
    vtkSmartPointer<vtkActor> m_previewFaceActor;
    vtkSmartPointer<vtkActor> m_previewEdgeActor;
    vtkSmartPointer<vtkActor> m_constructionPreviewActor;
    vtkSmartPointer<vtkActor> m_selectionEdgeActor;
    vtkSmartPointer<vtkActor> m_markerActor;
    vtkSmartPointer<vtkActor> m_selectedMarkerActor;
    vtkSmartPointer<vtkActor> m_rubberLineActor;
    vtkSmartPointer<vtkActor> m_constructionGuideActor;
    vtkSmartPointer<vtkActor> m_tangentGuideActor;
    vtkSmartPointer<vtkActor2D> m_snapActor;
    vtkSmartPointer<vtkActor> m_selectionBoxActor;
    vtkSmartPointer<vtkTextActor> m_cursorTextActor;
    vtkSmartPointer<vtkTextActor> m_hoverPointTextActor;
    vtkSmartPointer<vtkTextActor> m_fixedPointTextActor;
    vtkSmartPointer<vtkPoints> m_rubberLinePoints;
    vtkSmartPointer<vtkPolyData> m_rubberLinePolyData;
    vtkSmartPointer<vtkPoints> m_constructionGuidePoints;
    vtkSmartPointer<vtkPolyData> m_constructionGuidePolyData;
    vtkSmartPointer<vtkPoints> m_tangentGuidePoints;
    vtkSmartPointer<vtkPolyData> m_tangentGuidePolyData;
    vtkSmartPointer<vtkPoints> m_snapPoints;
    vtkSmartPointer<vtkPoints> m_selectionBoxPoints;
    vtkSmartPointer<vtkPolyData> m_snapPolyData;
    vtkSmartPointer<vtkPolyData> m_selectionBoxPolyData;
    std::vector<vtkSmartPointer<vtkTextActor>> m_tickActors;
};

} // namespace GeometrySketch
