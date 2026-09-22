#include "SketchViewController.h"

#include "OccToVtkConverter.h"
#include <ServiceRegistry.h>
#include <services/VisualWidgetService.h>

#include <TopoDS_Shape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp_Explorer.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRep_Tool.hxx>
#include <GeomAbs_CurveType.hxx>
#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>
#include <Standard_Failure.hxx>
#include <vtkActor.h>
#include <vtkCallbackCommand.h>
#include <vtkCamera.h>
#include <vtkCellArray.h>
#include <vtkCommand.h>
#include <vtkNew.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderer.h>
#include <vtkRenderWindow.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkTextActor.h>
#include <vtkTextProperty.h>

#include <QTimer>

#include <algorithm>
#include <cmath>

namespace GeometrySketch
{

namespace
{

VisualWidgetService* readyVisualService()
{
    auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>();
    return (vis && vis->renderer()) ? vis : nullptr;
}

double niceGridStep(double rawStep)
{
    if (rawStep <= 0.0)
        return 1.0;

    const double exponent = std::floor(std::log10(rawStep));
    const double base = std::pow(10.0, exponent);
    const double normalized = rawStep / base;
    if (normalized <= 1.0)
        return base;
    if (normalized <= 2.0)
        return 2.0 * base;
    if (normalized <= 5.0)
        return 5.0 * base;
    return 10.0 * base;
}

int decimalsForStep(double step)
{
    if (step <= 0.0 || !std::isfinite(step))
        return 2;

    const int decimals = static_cast<int>(std::ceil(-std::log10(step))) + 1;
    return std::clamp(decimals, 0, 8);
}

QString formatTick(double value, double step)
{
    if (std::abs(value) <= std::max(step, 1.0) * 1.0e-10)
        value = 0.0;

    const int decimals = decimalsForStep(step);
    QString text = QString::number(value, 'f', decimals);
    while (text.contains('.') && text.endsWith('0'))
        text.chop(1);
    if (text.endsWith('.'))
        text.chop(1);
    return text;
}

// Live sketch editing must not depend on TKIVtk's presentation filter for open wires.
// A freshly-created profile already contains the confirmed OCC edges, but the
// presentation filter can transiently omit the newest free edge.  Build the
// sketch line polydata directly from every TopoDS_Edge instead.
vtkSmartPointer<vtkPolyData> buildSketchEdgePolyData(const TopoDS_Shape& shape)
{
    auto poly = vtkSmartPointer<vtkPolyData>::New();
    auto points = vtkSmartPointer<vtkPoints>::New();
    auto lines = vtkSmartPointer<vtkCellArray>::New();
    poly->SetPoints(points);
    poly->SetLines(lines);

    if (shape.IsNull())
        return poly;

    constexpr double kPi = 3.14159265358979323846;

    for (TopExp_Explorer explorer(shape, TopAbs_EDGE); explorer.More(); explorer.Next())
    {
        const TopoDS_Edge edge = TopoDS::Edge(explorer.Current());
        if (edge.IsNull() || BRep_Tool::Degenerated(edge))
            continue;

        try
        {
            BRepAdaptor_Curve curve(edge);
            const double first = curve.FirstParameter();
            const double last = curve.LastParameter();
            if (!std::isfinite(first) || !std::isfinite(last) ||
                std::abs(last - first) <= 1.0e-14)
            {
                continue;
            }

            int segmentCount = 1;
            switch (curve.GetType())
            {
            case GeomAbs_Line:
                segmentCount = 1;
                break;
            case GeomAbs_Circle:
            case GeomAbs_Ellipse:
            {
                // About 5.6 degrees per segment, bounded for interactive use.
                const double sweep = std::abs(last - first);
                segmentCount = std::clamp(
                    static_cast<int>(std::ceil(sweep / (kPi / 32.0))), 8, 128);
                break;
            }
            case GeomAbs_BSplineCurve:
            case GeomAbs_BezierCurve:
                segmentCount = 48;
                break;
            default:
                segmentCount = 24;
                break;
            }

            vtkIdType previousId = -1;
            for (int i = 0; i <= segmentCount; ++i)
            {
                const double t = first + (last - first) *
                                             (static_cast<double>(i) /
                                              static_cast<double>(segmentCount));
                const gp_Pnt point = curve.Value(t);
                const vtkIdType currentId = points->InsertNextPoint(point.X(), point.Y(), point.Z());
                if (previousId >= 0)
                {
                    lines->InsertNextCell(2);
                    lines->InsertCellPoint(previousId);
                    lines->InsertCellPoint(currentId);
                }
                previousId = currentId;
            }
        }
        catch (const Standard_Failure&)
        {
            // One malformed preview edge must not blank the rest of the sketch.
        }
        catch (...)
        {
        }
    }

    poly->Modified();
    return poly;
}

} // namespace

SketchViewController::~SketchViewController()
{
    exit();
}

void SketchViewController::setObjectPrefix(const QString& uuid)
{
    m_uuid = uuid;
}

void SketchViewController::setPlaneSettings(const PlaneSettings& settings)
{
    const bool planeChanged = (m_settings.plane != settings.plane) ||
                              (std::abs(m_settings.offset - settings.offset) > 1.0e-12);
    const bool gridChanged = std::abs(m_settings.gridStep - settings.gridStep) > 1.0e-12;
    const bool axesChanged = m_settings.showAxes != settings.showAxes;
    m_settings = settings;
    if (m_active && (planeChanged || gridChanged || axesChanged))
    {
        if (planeChanged)
            alignCameraToPlane();
        redrawGrid();
    }
}

void SketchViewController::setBoxSelectionEnabled(bool enabled)
{
    m_boxSelectionEnabled = enabled;
    if (!enabled)
    {
        m_pressButtonEvent = 0;
        m_draggingSelectionBox = false;
        m_draggingPoint = false;
        updateSelectionBox(false);
    }
}

QString SketchViewController::key(const QString& suffix) const
{
    return m_uuid + QStringLiteral("_sketch_") + suffix;
}

void SketchViewController::enter()
{
    if (m_active)
    {
        if (auto* vis = readyVisualService())
            vis->renderer()->UseFXAAOn();
        alignCameraToPlane();
        checkRenderWindowSize(true);
        redrawGrid();
        return;
    }

    m_active = true;
    m_lastMoveX = -1;
    m_lastMoveY = -1;
    m_renderClock.start();
    if (auto* vis = readyVisualService())
    {
        m_previousUseFxaa = vis->renderer()->GetUseFXAA();
        m_hasPreviousUseFxaa = true;
        vis->renderer()->UseFXAAOn();
    }
    setupObservers();
    startResizeWatch();
    alignCameraToPlane();
    redrawGrid();

}

void SketchViewController::exit()
{
    stopResizeWatch();
    clearPreview();
    clearGrid();
    teardownObservers();
    if (m_hasPreviousUseFxaa)
    {
        if (auto* vis = readyVisualService())
        {
            vis->renderer()->SetUseFXAA(m_previousUseFxaa);
            vis->render();
        }
        m_hasPreviousUseFxaa = false;
    }
    m_active = false;
}

void SketchViewController::startResizeWatch()
{
    if (!m_resizeTimer)
    {
        m_resizeTimer = std::make_unique<QTimer>();
        m_resizeTimer->setInterval(120);
        QObject::connect(m_resizeTimer.get(), &QTimer::timeout, [this]
        {
            checkRenderWindowSize(false);
        });
    }
    checkRenderWindowSize(true);
    m_resizeTimer->start();
}

void SketchViewController::stopResizeWatch()
{
    if (m_resizeTimer)
        m_resizeTimer->stop();
    m_lastWindowWidth = -1;
    m_lastWindowHeight = -1;
}

void SketchViewController::checkRenderWindowSize(bool force)
{
    if (!m_active || m_redrawingGrid)
        return;

    auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!vis || !vis->interactor() || !vis->interactor()->GetRenderWindow())
        return;

    int* size = vis->interactor()->GetRenderWindow()->GetSize();
    if (!size)
        return;

    const int w = std::max(1, size[0]);
    const int h = std::max(1, size[1]);
    if (!force && w == m_lastWindowWidth && h == m_lastWindowHeight)
        return;

    m_lastWindowWidth = w;
    m_lastWindowHeight = h;
    if (!force)
        redrawGrid();
}

void SketchViewController::setupObservers()
{
    teardownObservers();

    auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!vis || !vis->interactor())
        return;

    auto* interactor = vis->interactor();

    m_pressCallback = vtkSmartPointer<vtkCallbackCommand>::New();
    m_pressCallback->SetClientData(this);
    m_pressCallback->SetCallback([](vtkObject* caller, unsigned long eventId, void* clientData, void*)
    {
        auto* self = static_cast<SketchViewController*>(clientData);
        if (self)
            self->onMousePress(caller, eventId);
    });

    m_releaseCallback = vtkSmartPointer<vtkCallbackCommand>::New();
    m_releaseCallback->SetClientData(this);
    m_releaseCallback->SetCallback([](vtkObject* caller, unsigned long eventId, void* clientData, void*)
    {
        auto* self = static_cast<SketchViewController*>(clientData);
        if (self)
            self->onMouseRelease(caller, eventId);
    });

    m_moveCallback = vtkSmartPointer<vtkCallbackCommand>::New();
    m_moveCallback->SetClientData(this);
    m_moveCallback->SetCallback([](vtkObject* caller, unsigned long eventId, void* clientData, void*)
    {
        auto* self = static_cast<SketchViewController*>(clientData);
        if (self)
            self->onMouseMove(caller, eventId);
    });

    m_viewCallback = vtkSmartPointer<vtkCallbackCommand>::New();
    m_viewCallback->SetClientData(this);
    m_viewCallback->SetCallback([](vtkObject*, unsigned long, void* clientData, void*)
    {
        auto* self = static_cast<SketchViewController*>(clientData);
        if (!self || self->m_redrawingGrid)
            return;
        self->checkRenderWindowSize(true);
        self->redrawGrid();
    });

    m_leftPressTag = interactor->AddObserver(vtkCommand::LeftButtonPressEvent, m_pressCallback, 10.0f);
    m_rightPressTag = interactor->AddObserver(vtkCommand::RightButtonPressEvent, m_pressCallback, 10.0f);
    m_leftReleaseTag = interactor->AddObserver(vtkCommand::LeftButtonReleaseEvent, m_releaseCallback, 10.0f);
    m_rightReleaseTag = interactor->AddObserver(vtkCommand::RightButtonReleaseEvent, m_releaseCallback, 10.0f);
    m_moveTag = interactor->AddObserver(vtkCommand::MouseMoveEvent, m_moveCallback, 10.0f);
    m_wheelForwardTag = interactor->AddObserver(vtkCommand::MouseWheelForwardEvent, m_viewCallback, -1.0f);
    m_wheelBackwardTag = interactor->AddObserver(vtkCommand::MouseWheelBackwardEvent, m_viewCallback, -1.0f);
    // 窗口大小变化时，RenderWindow 尺寸会变，但旧网格不会自动重算。
    // 监听 ConfigureEvent，按新的宽高重新铺满背景网格。
    m_configureTag = interactor->AddObserver(vtkCommand::ConfigureEvent, m_viewCallback, -1.0f);
    if (vis->renderer() && vis->renderer()->GetActiveCamera())
    {
        m_observedCamera = vis->renderer()->GetActiveCamera();
        m_cameraModifiedTag = m_observedCamera->AddObserver(vtkCommand::ModifiedEvent, m_viewCallback, -1.0f);
    }
}

void SketchViewController::teardownObservers()
{
    if (m_observedCamera && m_cameraModifiedTag)
        m_observedCamera->RemoveObserver(m_cameraModifiedTag);

    auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (vis && vis->interactor())
    {
        auto* interactor = vis->interactor();
        if (m_leftPressTag) interactor->RemoveObserver(m_leftPressTag);
        if (m_rightPressTag) interactor->RemoveObserver(m_rightPressTag);
        if (m_leftReleaseTag) interactor->RemoveObserver(m_leftReleaseTag);
        if (m_rightReleaseTag) interactor->RemoveObserver(m_rightReleaseTag);
        if (m_moveTag) interactor->RemoveObserver(m_moveTag);
        if (m_wheelForwardTag) interactor->RemoveObserver(m_wheelForwardTag);
        if (m_wheelBackwardTag) interactor->RemoveObserver(m_wheelBackwardTag);
        if (m_configureTag) interactor->RemoveObserver(m_configureTag);
    }

    m_leftPressTag = 0;
    m_rightPressTag = 0;
    m_leftReleaseTag = 0;
    m_rightReleaseTag = 0;
    m_moveTag = 0;
    m_wheelForwardTag = 0;
    m_wheelBackwardTag = 0;
    m_configureTag = 0;
    m_cameraModifiedTag = 0;
    m_pressCallback = nullptr;
    m_releaseCallback = nullptr;
    m_moveCallback = nullptr;
    m_viewCallback = nullptr;
    m_observedCamera = nullptr;
}

void SketchViewController::onMousePress(vtkObject* caller, unsigned long eventId)
{
    auto* interactor = vtkRenderWindowInteractor::SafeDownCast(caller);
    if (!interactor)
        return;

    interactor->GetEventPosition(m_pressX, m_pressY);
    m_pressButtonEvent = eventId;
    m_draggingSelectionBox = false;
    m_draggingPoint = false;
    const bool hasPlanePoint = screenToUv(m_pressX, m_pressY, m_pressU, m_pressV);

    if (m_boxSelectionEnabled && eventId == vtkCommand::LeftButtonPressEvent &&
        hasPlanePoint && pointDragStartHandler)
    {
        m_draggingPoint = pointDragStartHandler(m_pressU, m_pressV);
    }

    if (m_boxSelectionEnabled && !m_draggingPoint &&
        eventId == vtkCommand::LeftButtonPressEvent)
        updateSelectionBox(false);

    if (m_pressCallback)
        m_pressCallback->AbortFlagOn();
}

void SketchViewController::onMouseRelease(vtkObject* caller, unsigned long eventId)
{
    auto* interactor = vtkRenderWindowInteractor::SafeDownCast(caller);
    if (!interactor)
        return;

    int x = 0;
    int y = 0;
    interactor->GetEventPosition(x, y);
    double u = 0.0;
    double v = 0.0;

    const int dx = x - m_pressX;
    const int dy = y - m_pressY;
    const bool click = dx * dx + dy * dy <= 16;
    const bool leftRelease = eventId == vtkCommand::LeftButtonReleaseEvent;

    if (m_draggingPoint && leftRelease &&
        m_pressButtonEvent == vtkCommand::LeftButtonPressEvent)
    {
        if (!screenToUv(x, y, u, v))
        {
            u = m_pressU;
            v = m_pressV;
        }
        if (pointDragEndHandler)
            pointDragEndHandler(u, v);
        m_pressButtonEvent = 0;
        m_draggingSelectionBox = false;
        m_draggingPoint = false;
        updateSelectionBox(false);
        if (m_releaseCallback)
            m_releaseCallback->AbortFlagOn();
        return;
    }

    if (m_boxSelectionEnabled && leftRelease && m_pressButtonEvent == vtkCommand::LeftButtonPressEvent)
    {
        if (!click && screenToUv(x, y, u, v) && boxSelectHandler)
        {
            const bool crossing = x < m_pressX;
            const bool additive = interactor->GetControlKey() != 0 || interactor->GetShiftKey() != 0;
            boxSelectHandler(m_pressU, m_pressV, u, v, crossing, additive);
        }
        else if (click && screenToUv(x, y, u, v) && clickHandler)
        {
            clickHandler(u, v, false);
        }
        updateSelectionBox(false);
    }
    else if (click)
    {
        if (screenToUv(x, y, u, v) && clickHandler)
            clickHandler(u, v, eventId == vtkCommand::RightButtonReleaseEvent);
    }

    m_pressButtonEvent = 0;
    m_draggingSelectionBox = false;
    m_draggingPoint = false;
    if (m_releaseCallback)
        m_releaseCallback->AbortFlagOn();
}

void SketchViewController::onMouseMove(vtkObject* caller, unsigned long)
{
    auto* interactor = vtkRenderWindowInteractor::SafeDownCast(caller);
    if (!interactor)
        return;

    int x = 0;
    int y = 0;
    interactor->GetEventPosition(x, y);
    if (x == m_lastMoveX && y == m_lastMoveY)
        return;
    if (m_rubberLineActor && m_renderClock.isValid() && m_renderClock.elapsed() < 8)
        return;
    m_lastMoveX = x;
    m_lastMoveY = y;

    double u = 0.0;
    double v = 0.0;
    if (screenToUv(x, y, u, v))
    {
        if (m_draggingPoint && m_pressButtonEvent == vtkCommand::LeftButtonPressEvent)
        {
            if (pointDragMoveHandler)
                pointDragMoveHandler(u, v);
            if (m_moveCallback)
                m_moveCallback->AbortFlagOn();
            return;
        }

        if (m_boxSelectionEnabled && m_pressButtonEvent == vtkCommand::LeftButtonPressEvent)
        {
            const int dx = x - m_pressX;
            const int dy = y - m_pressY;
            if (dx * dx + dy * dy > 16)
            {
                m_draggingSelectionBox = true;
                updateSelectionBox(true, {m_pressU, m_pressV}, {u, v}, x < m_pressX);
            }
        }
        if (moveHandler)
            moveHandler(u, v);
    }
    if (m_moveCallback)
        m_moveCallback->AbortFlagOn();
}

bool SketchViewController::screenToUv(int x, int y, double& u, double& v) const
{
    auto* vis = readyVisualService();
    if (!vis || !vis->renderer())
        return false;

    auto* renderer = vis->renderer();

    double pNear[4] = {0.0, 0.0, 0.0, 1.0};
    renderer->SetDisplayPoint(x, y, 0.0);
    renderer->DisplayToWorld();
    renderer->GetWorldPoint(pNear);
    if (std::abs(pNear[3]) > 1.0e-12)
    {
        pNear[0] /= pNear[3];
        pNear[1] /= pNear[3];
        pNear[2] /= pNear[3];
    }

    double pFar[4] = {0.0, 0.0, 0.0, 1.0};
    renderer->SetDisplayPoint(x, y, 1.0);
    renderer->DisplayToWorld();
    renderer->GetWorldPoint(pFar);
    if (std::abs(pFar[3]) > 1.0e-12)
    {
        pFar[0] /= pFar[3];
        pFar[1] /= pFar[3];
        pFar[2] /= pFar[3];
    }

    double t = 0.0;
    switch (m_settings.plane)
    {
    case Plane::XY:
    {
        const double diff = pFar[2] - pNear[2];
        if (std::abs(diff) < 1.0e-9) return false;
        t = (m_settings.offset - pNear[2]) / diff;
        u = pNear[0] + t * (pFar[0] - pNear[0]);
        v = pNear[1] + t * (pFar[1] - pNear[1]);
        return true;
    }
    case Plane::XZ:
    {
        const double diff = pFar[1] - pNear[1];
        if (std::abs(diff) < 1.0e-9) return false;
        t = (m_settings.offset - pNear[1]) / diff;
        u = pNear[0] + t * (pFar[0] - pNear[0]);
        v = pNear[2] + t * (pFar[2] - pNear[2]);
        return true;
    }
    case Plane::YZ:
    {
        const double diff = pFar[0] - pNear[0];
        if (std::abs(diff) < 1.0e-9) return false;
        t = (m_settings.offset - pNear[0]) / diff;
        u = pNear[1] + t * (pFar[1] - pNear[1]);
        v = pNear[2] + t * (pFar[2] - pNear[2]);
        return true;
    }
    }
    return false;
}

void SketchViewController::alignCameraToPlane()
{
    auto* vis = readyVisualService();
    if (!vis || !vis->renderer())
    {
        m_redrawingGrid = false;
        return;
    }

    auto* renderer = vis->renderer();
    auto* camera = renderer->GetActiveCamera();
    if (!camera)
        return;

    const double distance = 150.0;
    double focal[3] = {0.0, 0.0, 0.0};
    double pos[3] = {0.0, 0.0, 0.0};
    double up[3] = {0.0, 1.0, 0.0};

    switch (m_settings.plane)
    {
    case Plane::XY:
        focal[2] = m_settings.offset;
        pos[2] = m_settings.offset + distance;
        up[1] = 1.0;
        break;
    case Plane::XZ:
        focal[1] = m_settings.offset;
        pos[1] = m_settings.offset + distance;
        up[1] = 0.0;
        up[2] = 1.0;
        break;
    case Plane::YZ:
        focal[0] = m_settings.offset;
        pos[0] = m_settings.offset + distance;
        up[1] = 0.0;
        up[2] = 1.0;
        break;
    }

    camera->SetFocalPoint(focal);
    camera->SetPosition(pos);
    camera->SetViewUp(up);
    camera->ParallelProjectionOn();

    // 草图模式不要跟随场景包围盒 ResetCamera，避免已有几何把视图拉远。
    camera->SetParallelScale(60.0);
    renderer->ResetCameraClippingRange();
    vis->render();
}

bool SketchViewController::fitToPoints(const std::vector<Point2D>& points, double margin)
{
    if (points.empty())
        return false;

    double minU = points.front().u;
    double maxU = points.front().u;
    double minV = points.front().v;
    double maxV = points.front().v;
    for (const Point2D& point : points)
    {
        minU = std::min(minU, point.u);
        maxU = std::max(maxU, point.u);
        minV = std::min(minV, point.v);
        maxV = std::max(maxV, point.v);
    }

    auto* vis = readyVisualService();
    if (!vis || !vis->renderer())
        return false;

    auto* renderer = vis->renderer();
    auto* camera = renderer->GetActiveCamera();
    if (!camera)
        return false;

    margin = std::clamp(margin, 1.05, 3.0);
    const double centerU = 0.5 * (minU + maxU);
    const double centerV = 0.5 * (minV + maxV);
    const double spanU = std::max(0.0, maxU - minU);
    const double spanV = std::max(0.0, maxV - minV);

    double aspect = 1.0;
    if (vis->interactor() && vis->interactor()->GetRenderWindow())
    {
        int* size = vis->interactor()->GetRenderWindow()->GetSize();
        if (size && size[0] > 0 && size[1] > 0)
            aspect = static_cast<double>(size[0]) / static_cast<double>(size[1]);
    }
    aspect = std::max(0.1, aspect);

    // ParallelScale 表示视口垂直方向的一半。横向尺寸需要除以宽高比后再参与比较。
    const double contentHalfV = 0.5 * std::max(spanV, spanU / aspect);
    const double minimumHalfV = std::max(5.0, std::max(1.0e-6, m_settings.gridStep) * 5.0);
    const double parallelScale = std::max(minimumHalfV, contentHalfV * margin);
    const double cameraDistance = std::max(150.0, std::max(spanU, spanV) * 4.0 + 20.0);

    const gp_Pnt focalPoint = toWorldPoint(m_settings, centerU, centerV);
    double focal[3] = {focalPoint.X(), focalPoint.Y(), focalPoint.Z()};
    double pos[3] = {focal[0], focal[1], focal[2]};
    double up[3] = {0.0, 1.0, 0.0};

    switch (m_settings.plane)
    {
    case Plane::XY:
        pos[2] += cameraDistance;
        break;
    case Plane::XZ:
        pos[1] += cameraDistance;
        up[1] = 0.0;
        up[2] = 1.0;
        break;
    case Plane::YZ:
        pos[0] += cameraDistance;
        up[1] = 0.0;
        up[2] = 1.0;
        break;
    }

    // 相机每改一个参数都会触发 ModifiedEvent。调整期间先阻止网格重复重建，
    // 最后只按新的视口范围重画一次。
    const bool wasRedrawing = m_redrawingGrid;
    m_redrawingGrid = true;
    camera->SetFocalPoint(focal);
    camera->SetPosition(pos);
    camera->SetViewUp(up);
    camera->ParallelProjectionOn();
    camera->SetParallelScale(parallelScale);
    renderer->ResetCameraClippingRange();
    m_redrawingGrid = wasRedrawing;

    if (m_active && !wasRedrawing)
        redrawGrid();
    else
        vis->render();
    return true;
}

bool SketchViewController::fitToShape(const TopoDS_Shape& shape, double margin)
{
    if (shape.IsNull())
        return false;

    try
    {
        Bnd_Box box;
        BRepBndLib::Add(shape, box);
        if (box.IsVoid())
            return false;

        Standard_Real xmin = 0.0;
        Standard_Real ymin = 0.0;
        Standard_Real zmin = 0.0;
        Standard_Real xmax = 0.0;
        Standard_Real ymax = 0.0;
        Standard_Real zmax = 0.0;
        box.Get(xmin, ymin, zmin, xmax, ymax, zmax);

        std::vector<Point2D> corners;
        corners.reserve(4);
        switch (m_settings.plane)
        {
        case Plane::XY:
            corners = {{xmin, ymin}, {xmin, ymax}, {xmax, ymin}, {xmax, ymax}};
            break;
        case Plane::XZ:
            corners = {{xmin, zmin}, {xmin, zmax}, {xmax, zmin}, {xmax, zmax}};
            break;
        case Plane::YZ:
            corners = {{ymin, zmin}, {ymin, zmax}, {ymax, zmin}, {ymax, zmax}};
            break;
        }
        return fitToPoints(corners, margin);
    }
    catch (const Standard_Failure&)
    {
        return false;
    }
    catch (...)
    {
        return false;
    }
}

void SketchViewController::redrawGrid()
{
    if (m_redrawingGrid)
        return;
    m_redrawingGrid = true;
    clearGrid(false);

    auto* vis = readyVisualService();
    if (!vis || !vis->renderer())
    {
        m_redrawingGrid = false;
        return;
    }

    auto* renderer = vis->renderer();
    auto* camera = renderer->GetActiveCamera();

    double centerU = 0.0;
    double centerV = 0.0;
    double halfV = 60.0;
    double aspect = 1.0;

    if (camera)
    {
        double focal[3] = {0.0, 0.0, 0.0};
        camera->GetFocalPoint(focal);
        uvFromWorld(m_settings, focal, centerU, centerV);
        halfV = std::max(10.0, camera->GetParallelScale());
    }

    int* viewSize = vis->interactor() && vis->interactor()->GetRenderWindow()
                        ? vis->interactor()->GetRenderWindow()->GetSize()
                        : nullptr;
    const int width = viewSize ? std::max(1, viewSize[0]) : 1200;
    const int height = viewSize ? std::max(1, viewSize[1]) : 800;
    if (camera)
    {
        aspect = static_cast<double>(width) / static_cast<double>(height);
    }

    const double halfU = halfV * aspect;
    const double viewMinU = centerU - halfU;
    const double viewMaxU = centerU + halfU;
    const double viewMinV = centerV - halfV;
    const double viewMaxV = centerV + halfV;
    const double minorStep = std::max(1.0e-9, m_settings.gridStep);
    const double minU = std::floor(viewMinU / minorStep) * minorStep;
    const double maxU = std::ceil(viewMaxU / minorStep) * minorStep;
    const double minV = std::floor(viewMinV / minorStep) * minorStep;
    const double maxV = std::ceil(viewMaxV / minorStep) * minorStep;
    m_lastWindowWidth = width;
    m_lastWindowHeight = height;
    const double firstGridU = std::ceil(minU / minorStep) * minorStep;
    const double firstGridV = std::ceil(minV / minorStep) * minorStep;

    vtkNew<vtkPoints> gridPoints;
    vtkNew<vtkCellArray> gridLines;

    for (double u = firstGridU; u <= maxU + minorStep * 0.5; u += minorStep)
    {
        const gp_Pnt p1 = toWorldPoint(m_settings, u, minV);
        const gp_Pnt p2 = toWorldPoint(m_settings, u, maxV);
        const vtkIdType id1 = gridPoints->InsertNextPoint(p1.X(), p1.Y(), p1.Z());
        const vtkIdType id2 = gridPoints->InsertNextPoint(p2.X(), p2.Y(), p2.Z());
        gridLines->InsertNextCell(2);
        gridLines->InsertCellPoint(id1);
        gridLines->InsertCellPoint(id2);
    }

    for (double v = firstGridV; v <= maxV + minorStep * 0.5; v += minorStep)
    {
        const gp_Pnt p1 = toWorldPoint(m_settings, minU, v);
        const gp_Pnt p2 = toWorldPoint(m_settings, maxU, v);
        const vtkIdType id1 = gridPoints->InsertNextPoint(p1.X(), p1.Y(), p1.Z());
        const vtkIdType id2 = gridPoints->InsertNextPoint(p2.X(), p2.Y(), p2.Z());
        gridLines->InsertNextCell(2);
        gridLines->InsertCellPoint(id1);
        gridLines->InsertCellPoint(id2);
    }

    vtkNew<vtkPolyData> gridPoly;
    gridPoly->SetPoints(gridPoints);
    gridPoly->SetLines(gridLines);

    vtkNew<vtkPolyDataMapper> gridMapper;
    gridMapper->SetInputData(gridPoly);
    // Sketch geometry, rubber lines and the grid live on the same work plane.
    // Keep the grid slightly behind so depth-buffer tie breaking cannot hide
    // freshly-created sketch segments until the next render/rebuild.
    gridMapper->SetResolveCoincidentTopologyToPolygonOffset();
    gridMapper->SetRelativeCoincidentTopologyLineOffsetParameters(1.0, 2.0);

    m_gridActor = vtkSmartPointer<vtkActor>::New();
    m_gridActor->SetMapper(gridMapper);
    m_gridActor->GetProperty()->SetColor(0.78, 0.83, 0.90);
    m_gridActor->GetProperty()->SetOpacity(0.22);
    m_gridActor->GetProperty()->SetLineWidth(1.0);
    m_gridActor->GetProperty()->LightingOff();
    vis->addProp(key(QStringLiteral("grid")), m_gridActor, "Geometry", key(QStringLiteral("grid")), false, false);

    auto makeAxis = [&](const QString& suffix, const Point2D& a, const Point2D& b, double r, double g, double bColor, vtkSmartPointer<vtkActor>& actor)
    {
        vtkNew<vtkPoints> pts;
        vtkNew<vtkCellArray> lines;
        const gp_Pnt p1 = toWorldPoint(m_settings, a.u, a.v);
        const gp_Pnt p2 = toWorldPoint(m_settings, b.u, b.v);
        pts->InsertNextPoint(p1.X(), p1.Y(), p1.Z());
        pts->InsertNextPoint(p2.X(), p2.Y(), p2.Z());
        lines->InsertNextCell(2);
        lines->InsertCellPoint(0);
        lines->InsertCellPoint(1);

        vtkNew<vtkPolyData> poly;
        poly->SetPoints(pts);
        poly->SetLines(lines);
        vtkNew<vtkPolyDataMapper> mapper;
        mapper->SetInputData(poly);
        actor = vtkSmartPointer<vtkActor>::New();
        actor->SetMapper(mapper);
        actor->GetProperty()->SetColor(r, g, bColor);
        actor->GetProperty()->SetOpacity(0.55);
        actor->GetProperty()->SetLineWidth(1.35);
        actor->GetProperty()->LightingOff();
        vis->addProp(key(suffix), actor, "Geometry", key(suffix), false, false);
    };

    enum class TextAnchor
    {
        LeftBottom,
        LeftCenter,
        CenterBottom,
        RightCenter
    };

    int tickIndex = 0;
    auto addGridText = [&](const QString& text, int x, int y, int fontSize,
                           double r, double g, double b, bool bold, TextAnchor anchor)
    {
        auto actor = vtkSmartPointer<vtkTextActor>::New();
        actor->SetInput(text.toUtf8().constData());
        actor->SetDisplayPosition(x, y);
        actor->GetTextProperty()->SetFontSize(fontSize);
        actor->GetTextProperty()->SetColor(r, g, b);
        actor->GetTextProperty()->SetOpacity(1.0);
        actor->GetTextProperty()->SetBackgroundOpacity(0.0);
        switch (anchor)
        {
        case TextAnchor::LeftBottom:
            actor->GetTextProperty()->SetJustificationToLeft();
            actor->GetTextProperty()->SetVerticalJustificationToBottom();
            break;
        case TextAnchor::LeftCenter:
            actor->GetTextProperty()->SetJustificationToLeft();
            actor->GetTextProperty()->SetVerticalJustificationToCentered();
            break;
        case TextAnchor::CenterBottom:
            actor->GetTextProperty()->SetJustificationToCentered();
            actor->GetTextProperty()->SetVerticalJustificationToBottom();
            break;
        case TextAnchor::RightCenter:
            actor->GetTextProperty()->SetJustificationToRight();
            actor->GetTextProperty()->SetVerticalJustificationToCentered();
            break;
        }
        if (bold)
            actor->GetTextProperty()->BoldOn();
        m_tickActors.push_back(actor);
        const QString tickKey = key(QStringLiteral("tick_%1").arg(tickIndex++));
        vis->addProp(tickKey, actor, "Geometry", tickKey, false, false);
    };
    auto addTick = [&](const QString& text, int x, int y, bool major, TextAnchor anchor)
    {
        addGridText(text, x, y, major ? 11 : 10, 0.0, 0.0, 0.0, false, anchor);
    };

    auto displayForUv = [&](double u, double v, double& x, double& y)
    {
        const gp_Pnt point = toWorldPoint(m_settings, u, v);
        renderer->SetWorldPoint(point.X(), point.Y(), point.Z(), 1.0);
        renderer->WorldToDisplay();
        double display[3] = {0.0, 0.0, 0.0};
        renderer->GetDisplayPoint(display);
        x = display[0];
        y = display[1];
    };
    auto xForU = [&](double value)
    {
        double x = 0.0;
        double y = 0.0;
        displayForUv(value, centerV, x, y);
        return static_cast<int>(std::round(x));
    };
    auto yForV = [&](double value)
    {
        double x = 0.0;
        double y = 0.0;
        displayForUv(centerU, value, x, y);
        return static_cast<int>(std::round(y));
    };

    QString uAxisName = QStringLiteral("X");
    QString vAxisName = QStringLiteral("Y");
    switch (m_settings.plane)
    {
    case Plane::XY:
        uAxisName = QStringLiteral("X");
        vAxisName = QStringLiteral("Y");
        break;
    case Plane::XZ:
        uAxisName = QStringLiteral("X");
        vAxisName = QStringLiteral("Z");
        break;
    case Plane::YZ:
        uAxisName = QStringLiteral("Y");
        vAxisName = QStringLiteral("Z");
        break;
    }

    if (m_settings.showAxes)
    {
        if (minV <= 0.0 && maxV >= 0.0)
            makeAxis(QStringLiteral("u_axis"), {minU, 0.0}, {maxU, 0.0}, 0.0, 0.0, 0.0, m_uAxisActor);
        if (minU <= 0.0 && maxU >= 0.0)
            makeAxis(QStringLiteral("v_axis"), {0.0, minV}, {0.0, maxV}, 0.0, 0.0, 0.0, m_vAxisActor);

        if (m_uAxisActor && viewMinV <= 0.0 && viewMaxV >= 0.0)
        {
            const int y = std::clamp(yForV(0.0) + 8, 22, std::max(22, height - 18));
            addGridText(uAxisName, std::max(18, width - 24), y, 12, 0.0, 0.0, 0.0, true, TextAnchor::LeftCenter);
        }
        if (m_vAxisActor && viewMinU <= 0.0 && viewMaxU >= 0.0)
        {
            const int x = std::clamp(xForU(0.0) + 8, 18, std::max(18, width - 24));
            addGridText(vAxisName, x, std::max(22, height - 24), 12, 0.0, 0.0, 0.0, true, TextAnchor::LeftBottom);
        }
    }

    const int bottomY = 6;
    const int leftX = 44;
    const int edgePadding = 28;
    auto labelStepForPixels = [&](double worldSpan, int pixelSpan)
    {
        const double unitsPerPixel = worldSpan / static_cast<double>(std::max(1, pixelSpan));
        const double targetStep = std::max(minorStep, unitsPerPixel * 72.0);
        const double multiple = niceGridStep(std::max(1.0, std::ceil(targetStep / minorStep)));
        return minorStep * multiple;
    };
    const double labelStepU = labelStepForPixels(viewMaxU - viewMinU, width);
    const double labelStepV = labelStepForPixels(viewMaxV - viewMinV, height);
    const double firstLabelU = std::ceil(minU / labelStepU) * labelStepU;
    const double firstLabelV = std::ceil(minV / labelStepV) * labelStepV;

    for (double u = firstLabelU; u <= maxU + labelStepU * 0.5; u += labelStepU)
    {
        const int x = xForU(u);
        if (x < edgePadding || x > width - edgePadding)
            continue;
        addTick(formatTick(u, labelStepU), x, bottomY, std::abs(u) <= labelStepU * 0.25, TextAnchor::CenterBottom);
    }

    for (double v = firstLabelV; v <= maxV + labelStepV * 0.5; v += labelStepV)
    {
        const int y = yForV(v);
        if (y < 22 || y > height - 20)
            continue;
        addTick(formatTick(v, labelStepV), leftX, y, std::abs(v) <= labelStepV * 0.25, TextAnchor::RightCenter);
    }

    vis->render();
    m_redrawingGrid = false;
}

void SketchViewController::clearGrid(bool renderNow)
{
    auto* vis = readyVisualService();
    if (vis)
    {
        vis->removeProp(key(QStringLiteral("grid")), false);
        vis->removeProp(key(QStringLiteral("u_axis")), false);
        vis->removeProp(key(QStringLiteral("v_axis")), false);
        for (int i = 0; i < static_cast<int>(m_tickActors.size()); ++i)
            vis->removeProp(key(QStringLiteral("tick_%1").arg(i)), false);
        if (renderNow)
            vis->render();
    }
    m_gridActor = nullptr;
    m_uAxisActor = nullptr;
    m_vAxisActor = nullptr;
    m_tickActors.clear();
}

void SketchViewController::updatePreview(const TopoDS_Shape& shape)
{
    auto* vis = readyVisualService();
    if (!vis)
        return;

    vis->removeProp(key(QStringLiteral("preview_face")), false);
    vis->removeProp(key(QStringLiteral("preview_edge")), false);
    m_previewFaceActor = nullptr;
    m_previewEdgeActor = nullptr;

    if (shape.IsNull())
    {
        vis->render();
        return;
    }

    auto display = OCC::BuildDisplayPolyData(shape);

    vtkNew<vtkPolyDataMapper> faceMapper;
    faceMapper->SetInputData(display.surface);
    faceMapper->ScalarVisibilityOff();
    // Closed-sketch face is only a faint hint. Keep it behind all sketch edges.
    faceMapper->SetResolveCoincidentTopologyToPolygonOffset();
    faceMapper->SetRelativeCoincidentTopologyPolygonOffsetParameters(1.0, 2.0);

    m_previewFaceActor = vtkSmartPointer<vtkActor>::New();
    m_previewFaceActor->SetMapper(faceMapper);
    m_previewFaceActor->GetProperty()->SetColor(0.0, 0.60, 0.85);
    // 草图阶段以线框为主，面只作为极淡的闭合提示，避免看起来像实体/云图。
    m_previewFaceActor->GetProperty()->SetOpacity(0.08);
    m_previewFaceActor->PickableOff();

    const auto sketchEdges = buildSketchEdgePolyData(shape);
    vtkNew<vtkPolyDataMapper> edgeMapper;
    edgeMapper->SetInputData(sketchEdges);
    edgeMapper->ScalarVisibilityOff();
    edgeMapper->SetResolveCoincidentTopologyToPolygonOffset();
    edgeMapper->SetRelativeCoincidentTopologyLineOffsetParameters(-1.0, -4.0);

    m_previewEdgeActor = vtkSmartPointer<vtkActor>::New();
    m_previewEdgeActor->SetMapper(edgeMapper);
    // 草图正式线统一使用与普通几何一致的深色。不要根据“编辑/选中/导入”状态
    // 在蓝色和黑色之间切换；选中反馈只交给控制点，线本身保持稳定。
    m_previewEdgeActor->GetProperty()->SetColor(0.10, 0.12, 0.15);
    m_previewEdgeActor->GetProperty()->SetLineWidth(2.0);
    m_previewEdgeActor->GetProperty()->SetRenderLinesAsTubes(1);
    m_previewEdgeActor->GetProperty()->LightingOff();
    m_previewEdgeActor->PickableOff();

    vis->addProp(key(QStringLiteral("preview_face")), m_previewFaceActor, "Geometry", key(QStringLiteral("preview_face")), false, false);
    vis->addProp(key(QStringLiteral("preview_edge")), m_previewEdgeActor, "Geometry", key(QStringLiteral("preview_edge")), false, false);
    vis->render();
}

void SketchViewController::updateConstructionPreview(const TopoDS_Shape& shape)
{
    auto* vis = readyVisualService();
    if (!vis)
        return;

    vis->removeProp(key(QStringLiteral("construction_preview")), false);
    m_constructionPreviewActor = nullptr;
    if (shape.IsNull())
    {
        vis->render();
        return;
    }

    const auto sketchEdges = buildSketchEdgePolyData(shape);
    vtkNew<vtkPolyDataMapper> mapper;
    mapper->SetInputData(sketchEdges);
    mapper->ScalarVisibilityOff();
    mapper->SetResolveCoincidentTopologyToPolygonOffset();
    mapper->SetRelativeCoincidentTopologyLineOffsetParameters(-1.0, -3.0);

    m_constructionPreviewActor = vtkSmartPointer<vtkActor>::New();
    m_constructionPreviewActor->SetMapper(mapper);
    m_constructionPreviewActor->GetProperty()->SetColor(0.48, 0.54, 0.62);
    m_constructionPreviewActor->GetProperty()->SetLineWidth(1.15);
    m_constructionPreviewActor->GetProperty()->SetOpacity(0.9);
    m_constructionPreviewActor->GetProperty()->LightingOff();
    m_constructionPreviewActor->PickableOff();

    vis->addProp(key(QStringLiteral("construction_preview")), m_constructionPreviewActor, "Geometry",
                 key(QStringLiteral("construction_preview")), false, false);
    vis->render();
}

void SketchViewController::updateSelectionHighlight(const TopoDS_Shape& shape)
{
    auto* vis = readyVisualService();
    if (!vis)
        return;

    vis->removeProp(key(QStringLiteral("selection_edge")), false);
    m_selectionEdgeActor = nullptr;

    if (shape.IsNull())
    {
        vis->render();
        return;
    }

    const auto sketchEdges = buildSketchEdgePolyData(shape);
    vtkNew<vtkPolyDataMapper> edgeMapper;
    edgeMapper->SetInputData(sketchEdges);
    edgeMapper->ScalarVisibilityOff();
    edgeMapper->SetResolveCoincidentTopologyToPolygonOffset();
    edgeMapper->SetRelativeCoincidentTopologyLineOffsetParameters(-1.0, -8.0);

    m_selectionEdgeActor = vtkSmartPointer<vtkActor>::New();
    m_selectionEdgeActor->SetMapper(edgeMapper);
    // 选中对象使用单一实线高亮，不再叠加半透明宽轮廓。
    // 选择层放在基础草图线前面并保持不透明，使被单选/框选的线看起来
    // 就是一条清晰的青色线；取消选择后基础深色线自然恢复。
    edgeMapper->SetRelativeCoincidentTopologyLineOffsetParameters(-1.0, -8.0);
    m_selectionEdgeActor->GetProperty()->SetColor(0.0, 0.68, 0.86);
    m_selectionEdgeActor->GetProperty()->SetOpacity(1.0);
    m_selectionEdgeActor->GetProperty()->SetLineWidth(2.2);
    m_selectionEdgeActor->GetProperty()->SetRenderLinesAsTubes(1);
    m_selectionEdgeActor->GetProperty()->LightingOff();
    m_selectionEdgeActor->PickableOff();

    vis->addProp(key(QStringLiteral("selection_edge")), m_selectionEdgeActor, "Geometry",
                 key(QStringLiteral("selection_edge")), false, false);
    vis->render();
}

void SketchViewController::clearSelectionHighlight()
{
    auto* vis = readyVisualService();
    if (vis)
        vis->removeProp(key(QStringLiteral("selection_edge")), true);
    m_selectionEdgeActor = nullptr;
}

void SketchViewController::updateSelectedPointMarkers(const std::vector<Point2D>& points)
{
    auto* vis = readyVisualService();
    if (!vis)
        return;

    vis->removeProp(key(QStringLiteral("selected_markers")), false);
    m_selectedMarkerActor = nullptr;

    if (points.empty())
    {
        vis->render();
        return;
    }

    vtkNew<vtkPoints> markerPoints;
    vtkNew<vtkCellArray> verts;
    for (const auto& p : points)
    {
        const gp_Pnt world = toWorldPoint(m_settings, p.u, p.v);
        const vtkIdType id = markerPoints->InsertNextPoint(world.X(), world.Y(), world.Z());
        verts->InsertNextCell(1);
        verts->InsertCellPoint(id);
    }

    vtkNew<vtkPolyData> poly;
    poly->SetPoints(markerPoints);
    poly->SetVerts(verts);

    vtkNew<vtkPolyDataMapper> mapper;
    mapper->SetInputData(poly);

    m_selectedMarkerActor = vtkSmartPointer<vtkActor>::New();
    m_selectedMarkerActor->SetMapper(mapper);
    m_selectedMarkerActor->GetProperty()->SetColor(0.0, 0.85, 1.0);
    m_selectedMarkerActor->GetProperty()->SetPointSize(7.0);
    m_selectedMarkerActor->GetProperty()->RenderPointsAsSpheresOn();
    m_selectedMarkerActor->GetProperty()->LightingOff();

    vis->addProp(key(QStringLiteral("selected_markers")), m_selectedMarkerActor, "Geometry",
                 key(QStringLiteral("selected_markers")), false, false);
    vis->render();
}

void SketchViewController::clearSelectedPointMarkers()
{
    auto* vis = readyVisualService();
    if (vis)
        vis->removeProp(key(QStringLiteral("selected_markers")), true);
    m_selectedMarkerActor = nullptr;
}

void SketchViewController::clearPreview()
{
    auto* vis = readyVisualService();
    if (vis)
    {
        vis->removeProp(key(QStringLiteral("preview_face")), false);
        vis->removeProp(key(QStringLiteral("preview_edge")), false);
        vis->removeProp(key(QStringLiteral("construction_preview")), false);
        vis->removeProp(key(QStringLiteral("markers")), false);
        vis->removeProp(key(QStringLiteral("rubber")), false);
        vis->removeProp(key(QStringLiteral("construction_guide")), false);
        vis->removeProp(key(QStringLiteral("tangent_guide")), false);
        vis->removeProp(key(QStringLiteral("snap")), false);
        vis->removeProp(key(QStringLiteral("selection_edge")), false);
        vis->removeProp(key(QStringLiteral("selected_markers")), false);
        vis->removeProp(key(QStringLiteral("selection_box")), false);
        vis->removeProp(key(QStringLiteral("cursor_text")), false);
        vis->removeProp(key(QStringLiteral("hover_point_text")), false);
        vis->removeProp(key(QStringLiteral("fixed_point_text")), false);
        vis->render();
    }
    m_previewFaceActor = nullptr;
    m_previewEdgeActor = nullptr;
    m_constructionPreviewActor = nullptr;
    m_selectionEdgeActor = nullptr;
    m_selectedMarkerActor = nullptr;
    m_markerActor = nullptr;
    m_rubberLineActor = nullptr;
    m_constructionGuideActor = nullptr;
    m_tangentGuideActor = nullptr;
    m_snapActor = nullptr;
    m_selectionBoxActor = nullptr;
    m_cursorTextActor = nullptr;
    m_hoverPointTextActor = nullptr;
    m_fixedPointTextActor = nullptr;
    m_constructionGuidePoints = nullptr;
    m_constructionGuidePolyData = nullptr;
    m_tangentGuidePoints = nullptr;
    m_tangentGuidePolyData = nullptr;
    m_snapPoints = nullptr;
    m_snapPolyData = nullptr;
    m_selectionBoxPoints = nullptr;
    m_selectionBoxPolyData = nullptr;
}

void SketchViewController::updatePointMarkers(const std::vector<Point2D>& points)
{
    auto* vis = readyVisualService();
    if (!vis)
        return;

    vis->removeProp(key(QStringLiteral("markers")), false);
    m_markerActor = nullptr;

    if (points.empty())
    {
        vis->render();
        return;
    }

    vtkNew<vtkPoints> markerPoints;
    vtkNew<vtkCellArray> verts;
    for (const auto& p : points)
    {
        const gp_Pnt world = toWorldPoint(m_settings, p.u, p.v);
        const vtkIdType id = markerPoints->InsertNextPoint(world.X(), world.Y(), world.Z());
        verts->InsertNextCell(1);
        verts->InsertCellPoint(id);
    }

    vtkNew<vtkPolyData> poly;
    poly->SetPoints(markerPoints);
    poly->SetVerts(verts);

    vtkNew<vtkPolyDataMapper> mapper;
    mapper->SetInputData(poly);

    m_markerActor = vtkSmartPointer<vtkActor>::New();
    m_markerActor->SetMapper(mapper);
    m_markerActor->GetProperty()->SetColor(0.10, 0.12, 0.15);
    m_markerActor->GetProperty()->SetPointSize(4.5);
    m_markerActor->GetProperty()->RenderPointsAsSpheresOn();
    m_markerActor->GetProperty()->LightingOff();

    vis->addProp(key(QStringLiteral("markers")), m_markerActor, "Geometry", key(QStringLiteral("markers")), false, false);
    vis->render();
}

void SketchViewController::updateRubberLine(bool visible, const Point2D& from, const Point2D& to)
{
    if (!visible)
    {
        updateRubberShape(false, {}, false);
        return;
    }
    updateRubberShape(true, {from, to}, false);
}

void SketchViewController::updateRubberShape(bool visible, const std::vector<Point2D>& points, bool closed)
{
    auto* vis = readyVisualService();
    if (!vis)
        return;

    const bool canDraw = visible && points.size() >= 2;
    if (!canDraw)
    {
        if (m_rubberLineActor)
        {
            vis->removeProp(key(QStringLiteral("rubber")), false);
            m_rubberLineActor = nullptr;
            m_rubberLinePoints = nullptr;
            m_rubberLinePolyData = nullptr;
            vis->render();
        }
        return;
    }

    const vtkIdType pointCount = static_cast<vtkIdType>(points.size());
    bool createdActor = false;
    if (!m_rubberLineActor || !m_rubberLinePoints || !m_rubberLinePolyData)
    {
        createdActor = true;
        m_rubberLinePoints = vtkSmartPointer<vtkPoints>::New();
        m_rubberLinePolyData = vtkSmartPointer<vtkPolyData>::New();

        vtkNew<vtkPolyDataMapper> mapper;
        mapper->SetInputData(m_rubberLinePolyData);
        mapper->SetResolveCoincidentTopologyToPolygonOffset();
        // The active orange segment must always stay above the normal sketch.
        mapper->SetRelativeCoincidentTopologyLineOffsetParameters(-1.0, -10.0);

        m_rubberLineActor = vtkSmartPointer<vtkActor>::New();
        m_rubberLineActor->SetMapper(mapper);
        m_rubberLineActor->GetProperty()->SetColor(0.92, 0.55, 0.05);
        m_rubberLineActor->GetProperty()->SetLineWidth(1.5);
        m_rubberLineActor->GetProperty()->LightingOff();

        vis->addProp(key(QStringLiteral("rubber")), m_rubberLineActor, "Geometry", key(QStringLiteral("rubber")), false, false);
    }

    m_rubberLinePoints->SetNumberOfPoints(pointCount);
    for (vtkIdType i = 0; i < pointCount; ++i)
    {
        const gp_Pnt world = toWorldPoint(m_settings, points[static_cast<std::size_t>(i)].u,
                                          points[static_cast<std::size_t>(i)].v);
        m_rubberLinePoints->SetPoint(i, world.X(), world.Y(), world.Z());
    }

    vtkNew<vtkCellArray> lines;
    const vtkIdType linePointCount = closed ? pointCount + 1 : pointCount;
    lines->InsertNextCell(linePointCount);
    for (vtkIdType i = 0; i < pointCount; ++i)
        lines->InsertCellPoint(i);
    if (closed)
        lines->InsertCellPoint(0);

    m_rubberLinePolyData->SetPoints(m_rubberLinePoints);
    m_rubberLinePolyData->SetLines(lines);
    m_rubberLinePoints->Modified();
    m_rubberLinePolyData->Modified();

    if (createdActor || !m_renderClock.isValid() || m_renderClock.elapsed() >= 16)
    {
        vis->render();
        m_renderClock.restart();
    }
}

void SketchViewController::updateConstructionGuide(bool visible, const std::vector<std::pair<Point2D, Point2D>>& segments)
{
    auto* vis = readyVisualService();
    if (!vis)
        return;

    const bool canDraw = visible && !segments.empty();
    if (!canDraw)
    {
        if (m_constructionGuideActor)
        {
            vis->removeProp(key(QStringLiteral("construction_guide")), false);
            m_constructionGuideActor = nullptr;
            m_constructionGuidePoints = nullptr;
            m_constructionGuidePolyData = nullptr;
            vis->render();
        }
        return;
    }

    bool createdActor = false;
    if (!m_constructionGuideActor || !m_constructionGuidePoints || !m_constructionGuidePolyData)
    {
        createdActor = true;
        m_constructionGuidePoints = vtkSmartPointer<vtkPoints>::New();
        m_constructionGuidePolyData = vtkSmartPointer<vtkPolyData>::New();

        vtkNew<vtkPolyDataMapper> mapper;
        mapper->SetInputData(m_constructionGuidePolyData);

        m_constructionGuideActor = vtkSmartPointer<vtkActor>::New();
        m_constructionGuideActor->SetMapper(mapper);
        m_constructionGuideActor->PickableOff();
        m_constructionGuideActor->GetProperty()->SetColor(0.42, 0.48, 0.55);
        m_constructionGuideActor->GetProperty()->SetOpacity(0.72);
        m_constructionGuideActor->GetProperty()->SetLineWidth(1.0);
        m_constructionGuideActor->GetProperty()->LightingOff();

        vis->addProp(key(QStringLiteral("construction_guide")), m_constructionGuideActor, "Geometry",
                     key(QStringLiteral("construction_guide")), false, false);
    }

    const vtkIdType pointCount = static_cast<vtkIdType>(segments.size() * 2);
    m_constructionGuidePoints->SetNumberOfPoints(pointCount);

    vtkNew<vtkCellArray> lines;
    vtkIdType id = 0;
    for (const auto& segment : segments)
    {
        const gp_Pnt p0 = toWorldPoint(m_settings, segment.first.u, segment.first.v);
        const gp_Pnt p1 = toWorldPoint(m_settings, segment.second.u, segment.second.v);
        m_constructionGuidePoints->SetPoint(id, p0.X(), p0.Y(), p0.Z());
        m_constructionGuidePoints->SetPoint(id + 1, p1.X(), p1.Y(), p1.Z());
        lines->InsertNextCell(2);
        lines->InsertCellPoint(id);
        lines->InsertCellPoint(id + 1);
        id += 2;
    }

    m_constructionGuidePolyData->SetPoints(m_constructionGuidePoints);
    m_constructionGuidePolyData->SetLines(lines);
    m_constructionGuidePoints->Modified();
    m_constructionGuidePolyData->Modified();

    if (createdActor || !m_renderClock.isValid() || m_renderClock.elapsed() >= 16)
    {
        vis->render();
        m_renderClock.restart();
    }
}

void SketchViewController::updateTangentGuideLine(bool visible, const Point2D& from, const Point2D& to)
{
    auto* vis = readyVisualService();
    if (!vis)
        return;

    if (!visible)
    {
        if (m_tangentGuideActor)
        {
            vis->removeProp(key(QStringLiteral("tangent_guide")), false);
            m_tangentGuideActor = nullptr;
            m_tangentGuidePoints = nullptr;
            m_tangentGuidePolyData = nullptr;
            vis->render();
        }
        return;
    }

    bool createdActor = false;
    if (!m_tangentGuideActor || !m_tangentGuidePoints || !m_tangentGuidePolyData)
    {
        createdActor = true;
        m_tangentGuidePoints = vtkSmartPointer<vtkPoints>::New();
        m_tangentGuidePolyData = vtkSmartPointer<vtkPolyData>::New();

        vtkNew<vtkPolyDataMapper> mapper;
        mapper->SetInputData(m_tangentGuidePolyData);

        m_tangentGuideActor = vtkSmartPointer<vtkActor>::New();
        m_tangentGuideActor->SetMapper(mapper);
        // Thin cyan direction line: it indicates the tangent inherited by the tangent-end arc.
        m_tangentGuideActor->GetProperty()->SetColor(0.0, 0.85, 1.0);
        m_tangentGuideActor->GetProperty()->SetLineWidth(1.4);
        m_tangentGuideActor->GetProperty()->LightingOff();

        vis->addProp(key(QStringLiteral("tangent_guide")), m_tangentGuideActor, "Geometry",
                     key(QStringLiteral("tangent_guide")), false, false);
    }

    m_tangentGuidePoints->SetNumberOfPoints(2);
    const gp_Pnt p0 = toWorldPoint(m_settings, from.u, from.v);
    const gp_Pnt p1 = toWorldPoint(m_settings, to.u, to.v);
    m_tangentGuidePoints->SetPoint(0, p0.X(), p0.Y(), p0.Z());
    m_tangentGuidePoints->SetPoint(1, p1.X(), p1.Y(), p1.Z());

    vtkNew<vtkCellArray> lines;
    lines->InsertNextCell(2);
    lines->InsertCellPoint(0);
    lines->InsertCellPoint(1);
    m_tangentGuidePolyData->SetPoints(m_tangentGuidePoints);
    m_tangentGuidePolyData->SetLines(lines);
    m_tangentGuidePoints->Modified();
    m_tangentGuidePolyData->Modified();

    if (createdActor || !m_renderClock.isValid() || m_renderClock.elapsed() >= 16)
    {
        vis->render();
        m_renderClock.restart();
    }
}

void SketchViewController::updateSnapIndicator(bool visible, const Point2D& point)
{
    auto* vis = readyVisualService();
    if (!vis)
        return;

    if (!visible)
    {
        if (m_snapActor)
        {
            vis->removeProp(key(QStringLiteral("snap")), false);
            m_snapActor = nullptr;
            m_snapPoints = nullptr;
            m_snapPolyData = nullptr;
            vis->render();
        }
        return;
    }

    bool createdActor = false;
    if (!m_snapActor || !m_snapPoints || !m_snapPolyData)
    {
        createdActor = true;
        m_snapPoints = vtkSmartPointer<vtkPoints>::New();
        m_snapPolyData = vtkSmartPointer<vtkPolyData>::New();

        vtkNew<vtkCellArray> verts;
        verts->InsertNextCell(1);
        verts->InsertCellPoint(0);
        m_snapPolyData->SetVerts(verts);

        vtkNew<vtkPolyDataMapper> mapper;
        mapper->SetInputData(m_snapPolyData);

        m_snapActor = vtkSmartPointer<vtkActor>::New();
        m_snapActor->SetMapper(mapper);
        m_snapActor->PickableOff();
        m_snapActor->GetProperty()->SetColor(1.0, 0.58, 0.16);
        m_snapActor->GetProperty()->SetOpacity(0.85);
        m_snapActor->GetProperty()->SetPointSize(7.0);
        m_snapActor->GetProperty()->RenderPointsAsSpheresOn();
        m_snapActor->GetProperty()->LightingOff();

        vis->addProp(key(QStringLiteral("snap")), m_snapActor, "Geometry", key(QStringLiteral("snap")), false, false);
    }

    const gp_Pnt world = toWorldPoint(m_settings, point.u, point.v);
    m_snapPoints->SetNumberOfPoints(1);
    m_snapPoints->SetPoint(0, world.X(), world.Y(), world.Z());
    m_snapPolyData->SetPoints(m_snapPoints);
    m_snapPoints->Modified();
    m_snapPolyData->Modified();

    if (createdActor || !m_renderClock.isValid() || m_renderClock.elapsed() >= 16)
    {
        vis->render();
        m_renderClock.restart();
    }
}

void SketchViewController::updateSelectionBox(bool visible, const Point2D& first, const Point2D& second, bool crossing)
{
    auto* vis = readyVisualService();
    if (!vis)
        return;

    if (!visible)
    {
        if (m_selectionBoxActor)
            vis->removeProp(key(QStringLiteral("selection_box")), true);
        m_selectionBoxActor = nullptr;
        m_selectionBoxPoints = nullptr;
        m_selectionBoxPolyData = nullptr;
        return;
    }

    if (!m_selectionBoxActor || !m_selectionBoxPoints || !m_selectionBoxPolyData)
    {
        m_selectionBoxPoints = vtkSmartPointer<vtkPoints>::New();
        m_selectionBoxPolyData = vtkSmartPointer<vtkPolyData>::New();
        vtkNew<vtkCellArray> lines;
        vtkIdType ids[5] = {0, 1, 2, 3, 0};
        lines->InsertNextCell(5, ids);
        m_selectionBoxPolyData->SetLines(lines);

        vtkNew<vtkPolyDataMapper> mapper;
        mapper->SetInputData(m_selectionBoxPolyData);
        m_selectionBoxActor = vtkSmartPointer<vtkActor>::New();
        m_selectionBoxActor->SetMapper(mapper);
        m_selectionBoxActor->PickableOff();
        m_selectionBoxActor->GetProperty()->SetLineWidth(1.6);
        m_selectionBoxActor->GetProperty()->SetOpacity(0.95);
        m_selectionBoxActor->GetProperty()->LightingOff();
        vis->addProp(key(QStringLiteral("selection_box")), m_selectionBoxActor, "Geometry",
                     key(QStringLiteral("selection_box")), false, false);
    }

    const Point2D corners[4] = {
        {first.u, first.v}, {second.u, first.v}, {second.u, second.v}, {first.u, second.v}};
    m_selectionBoxPoints->SetNumberOfPoints(4);
    for (int i = 0; i < 4; ++i)
    {
        const gp_Pnt world = toWorldPoint(m_settings, corners[i].u, corners[i].v);
        m_selectionBoxPoints->SetPoint(i, world.X(), world.Y(), world.Z());
    }
    m_selectionBoxPolyData->SetPoints(m_selectionBoxPoints);
    m_selectionBoxPoints->Modified();
    m_selectionBoxPolyData->Modified();
    if (crossing)
        m_selectionBoxActor->GetProperty()->SetColor(0.95, 0.52, 0.12);
    else
        m_selectionBoxActor->GetProperty()->SetColor(0.0, 0.58, 0.85);
    vis->render();
}

void SketchViewController::updatePointTextActor(vtkSmartPointer<vtkTextActor>& actor,
                                                 const QString& suffix,
                                                 bool visible,
                                                 const Point2D& point,
                                                 const QString& text,
                                                 bool fixed)
{
    auto* vis = readyVisualService();
    if (!vis)
        return;

    if (!visible || text.isEmpty())
    {
        if (actor)
            vis->removeProp(key(suffix), true);
        actor = nullptr;
        return;
    }

    const bool createdActor = !actor;
    if (!actor)
    {
        actor = vtkSmartPointer<vtkTextActor>::New();
        actor->PickableOff();
        auto* prop = actor->GetTextProperty();
        prop->SetFontFamilyToArial();
        prop->SetFontSize(fixed ? 14 : 13);
        prop->SetColor(fixed ? 0.0 : 0.12, fixed ? 0.38 : 0.12, fixed ? 0.65 : 0.12);
        prop->SetBackgroundColor(1.0, 1.0, 1.0);
        prop->SetBackgroundOpacity(fixed ? 0.92 : 0.78);
        prop->SetJustificationToLeft();
        prop->SetVerticalJustificationToBottom();
        vis->addProp(key(suffix), actor, "Geometry", key(suffix), false, false);
    }

    actor->SetInput(text.toUtf8().constData());
    const gp_Pnt world = toWorldPoint(m_settings, point.u, point.v);
    auto* renderer = vis->renderer();
    renderer->SetWorldPoint(world.X(), world.Y(), world.Z(), 1.0);
    renderer->WorldToDisplay();
    double display[3] = {0.0, 0.0, 0.0};
    renderer->GetDisplayPoint(display);
    actor->SetDisplayPosition(static_cast<int>(std::lround(display[0] + 10.0)),
                              static_cast<int>(std::lround(display[1] + 10.0)));
    // 鼠标移动时坐标文本更新频繁，最多按约 60 FPS 重绘；首次创建和固定标签立即显示。
    if (createdActor || fixed || !m_renderClock.isValid() || m_renderClock.elapsed() >= 16)
    {
        vis->render();
        m_renderClock.restart();
    }
}

void SketchViewController::updateCursorText(double u, double v, bool snapped)
{
    if (!std::isfinite(u) || !std::isfinite(v))
    {
        updatePointTextActor(m_cursorTextActor, QStringLiteral("cursor_text"), false, {}, QString(), false);
        return;
    }
    const QString text = QStringLiteral("U=%1  V=%2")
                             .arg(QString::number(u, 'f', 3), QString::number(v, 'f', 3));
    updatePointTextActor(m_cursorTextActor, QStringLiteral("cursor_text"), true, {u, v}, text, snapped);
}

void SketchViewController::updateHoverPointText(bool visible, const Point2D& point, const QString& text)
{
    const QString value = text.isEmpty()
                              ? QStringLiteral("U=%1  V=%2")
                                    .arg(QString::number(point.u, 'f', 3), QString::number(point.v, 'f', 3))
                              : text;
    updatePointTextActor(m_hoverPointTextActor, QStringLiteral("hover_point_text"), visible, point, value, false);
}

void SketchViewController::updateFixedPointText(bool visible, const Point2D& point, const QString& text)
{
    const QString value = text.isEmpty()
                              ? QStringLiteral("U=%1  V=%2")
                                    .arg(QString::number(point.u, 'f', 3), QString::number(point.v, 'f', 3))
                              : text;
    updatePointTextActor(m_fixedPointTextActor, QStringLiteral("fixed_point_text"), visible, point, value, true);
}

} // namespace GeometrySketch
