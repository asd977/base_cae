#include "CadBody.h"

#include <OccToVtkConverter.h>
#include <GeometryDisplayStyle.h>

#include <QUuid>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <vtkPolyData.h>

namespace
{
// CAD 屏幕显示精度。
// FreeCAD 的 Deviation / AngularDeflection 是“可调显示属性”，默认值并不意味着
// 所有模型都必须锁死在 0.5 / 28.5。当前项目没有把这两个参数暴露给用户，
// 因此这里给正常交互使用更细的一档，避免小圆角/圆弧轮廓出现明显折线。
//
// 注意：这里只提高 presentation tessellation，不修改精确 OCC B-Rep。
constexpr double kCadDisplayDeviation = 0.20;
constexpr double kCadDisplayAngularDeflectionDeg = 12.0;

// 工程批量恢复仍保留较粗的 fast 档，防止大型 STEP 在加载阶段三角形数量暴涨。
// fast Actor 可能在加载后继续使用，因此也不能粗到 1.0 / 28.5 那种明显可见折线。
constexpr double kCadFastDisplayDeviation = 0.60;
constexpr double kCadFastDisplayAngularDeflectionDeg = 20.0;

bool& fastDisplayFlag()
{
    static bool enabled = false;
    return enabled;
}
}

CadBody::CadBody(QString uuid, TopoDS_Shape* shape, QString name, bool createActors)
    : _shape(shape), m_uuid(std::move(uuid)), m_name(std::move(name))
{
    if (m_name.isEmpty())
        m_name = m_uuid;
    if (createActors)
        initActor();
}

CadBody::~CadBody()
{
    if (_shape)
    {
        delete _shape;
        _shape = nullptr;
    }
}

TopoDS_Shape* CadBody::getShape()
{
    return _shape;
}

void CadBody::setActor(vtkSmartPointer<vtkActor> surfActor,
                       vtkSmartPointer<vtkActor> edgeActor)
{
    m_surfActor = surfActor;
    m_edgeActor = edgeActor;
    m_presentationState = (m_surfActor || m_edgeActor)
        ? PresentationState::Materialized
        : PresentationState::Failed;
}

vtkSmartPointer<vtkActor> CadBody::getSurfActor()
{
    return m_surfActor;
}

vtkSmartPointer<vtkActor> CadBody::getEdgeActor()
{
    return m_edgeActor;
}

bool CadBody::materializePresentation()
{
    if (m_presentationState == PresentationState::Failed)
        return false;
    if (m_presentationState == PresentationState::NotMaterialized)
        initActor();
    return m_surfActor.GetPointer() != nullptr || m_edgeActor.GetPointer() != nullptr;
}

bool CadBody::hasActors() const
{
    return m_surfActor.GetPointer() != nullptr || m_edgeActor.GetPointer() != nullptr;
}

void CadBody::setDisplayColor(const QColor& color)
{
    if (!color.isValid())
        return;

    m_displayColor = color;
    m_hasDisplayColor = true;
    if (m_surfActor && m_surfActor->GetProperty())
        m_surfActor->GetProperty()->SetColor(color.redF(), color.greenF(), color.blueF());
}

QColor CadBody::displayColor() const
{
    return m_displayColor;
}

bool CadBody::hasDisplayColor() const
{
    return m_hasDisplayColor;
}

void CadBody::setFastDisplayMode(bool enabled)
{
    fastDisplayFlag() = enabled;
}

bool CadBody::fastDisplayMode()
{
    return fastDisplayFlag();
}

void CadBody::initActor()
{
    if (m_presentationState != PresentationState::NotMaterialized)
        return;
    if (!_shape || _shape->IsNull())
    {
        m_presentationState = PresentationState::Failed;
        return;
    }

    const bool fast = fastDisplayMode();

    TopTools_IndexedMapOfShape faceMap;
    TopExp::MapShapes(*_shape, TopAbs_FACE, faceMap);
    const bool hasFaces = faceMap.Extent() > 0;

    // 工程恢复时 fastDisplayMode 只降低显示三角化精度，不再省略 B-Rep 拓扑边。
    // 之前这里在 fast=true 且有 Face 时把 buildEdges 设为 false：工程打开阶段
    // 会先创建一个“只有 Surface、没有 Edge actor”的 CadBody，随后虽然退出 fast
    // 模式，但该 presentation 已经处于 Materialized 状态，不会再次 initActor()，
    // 因而工程重开后几何线条会永久消失，直到几何被重新创建。
    //
    // 隐藏节点本来就由 GeometryBuildUtils 延迟创建 Actor，所以这里始终生成 Edge
    // 只影响实际可见对象；同时仍保留 fast 的 0.60 / 20deg 粗显示档，避免把工程
    // 打开阶段直接退回到正常交互的高精度三角化成本。
    const bool buildEdges = true;

    // 圆弧/圆角的“折线感”来自显示三角化过粗，而不是 VTK 光照。
    // 正常模式使用更细的线性偏差 + 角度偏差；fast 模式仍保持明显更粗的一档。
    // 这样不会回到早期“所有模型一律超细”的性能问题，同时轮廓质量更接近 CAD Viewer。
    const double displayDeviation =
        fast ? kCadFastDisplayDeviation : kCadDisplayDeviation;
    const double displayAngularDeflectionDeg =
        fast ? kCadFastDisplayAngularDeflectionDeg : kCadDisplayAngularDeflectionDeg;

    auto disp = OCC::BuildDisplayPolyData(*_shape,
                                         displayDeviation,
                                         displayAngularDeflectionDeg,
                                         true,
                                         buildEdges);
    setDisplayPolyData(disp.surface, disp.edges);
}

void CadBody::setDisplayPolyData(vtkPolyData* surface, vtkPolyData* edges)
{
    vtkSmartPointer<vtkActor> surfActor;
    if (surface && surface->GetNumberOfCells() > 0)
    {
        auto surfMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
        surfMapper->SetInputData(surface);
        surfMapper->ScalarVisibilityOff();

        // CAD 显示网格在一次几何构建完成后不会原地修改；参数修改/布尔/重新导入
        // 都会重新创建 CadBody/mapper。VTK 的 Static 模式专门用于这种场景：
        // Render 时不再向每个 mapper 的上游重复传播 Update 请求。大型 STEP
        // 含几十/几百个独立实体时，这能明显削减每帧 mapper 管线开销。
        surfMapper->StaticOn();
        GeometryDisplayStyle::configureDefaultSurfaceMapper(surfMapper);

        surfActor = vtkSmartPointer<vtkActor>::New();
        surfActor->SetMapper(surfMapper);
        GeometryDisplayStyle::applyDefaultSurface(surfActor->GetProperty());

        if (m_hasDisplayColor && m_displayColor.isValid())
        {
            surfActor->GetProperty()->SetColor(m_displayColor.redF(),
                                               m_displayColor.greenF(),
                                               m_displayColor.blueF());
        }
    }

    vtkSmartPointer<vtkActor> edgeActor;
    if (edges && edges->GetNumberOfLines() > 0)
    {
        auto edgeMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
        edgeMapper->SetInputData(edges);
        edgeMapper->ScalarVisibilityOff();
        edgeMapper->StaticOn();

        // FreeCAD 的 Face/Edge 分离 presentation 保留；在 VTK 中使用其原生
        // line coincident-topology，只给可见 CAD Edge 极小的前向 depth priority。
        // Face 自身不再后移，避免掠射角下内部/背面边穿透。
        GeometryDisplayStyle::configureCoincidentEdgeMapper(edgeMapper);

        edgeActor = vtkSmartPointer<vtkActor>::New();
        edgeActor->SetMapper(edgeMapper);
        GeometryDisplayStyle::applyDefaultEdge(edgeActor->GetProperty());
    }

    m_surfActor = surfActor;
    m_edgeActor = edgeActor;
    m_presentationState = (m_surfActor || m_edgeActor)
        ? PresentationState::Materialized
        : PresentationState::Failed;
}
