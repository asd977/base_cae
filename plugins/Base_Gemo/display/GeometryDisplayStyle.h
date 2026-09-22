#pragma once

#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>

namespace GeometryDisplayStyle
{
// CAD presentation 保留 FreeCAD PartGui 的核心结构：Face 与 B-Rep Edge
// 来自同一次 OCC tessellation，Edge 使用 PolygonOnTriangulation 与 Face 共点。
//
// 但深度分离不能照抄 Coin3D 的 SoPolygonOffset 数值。FreeCAD/Coin3D 之所以
// “后移 filled Face”，是因为 Coin3D 明确说明 polygon offset 对真正的 SoLineSet
// 不生效；VTK OpenGL2 则原生支持 GL_LINES 的独立 coincident-topology offset。
// 因此这里采用更适合 VTK 的等价做法：
//   1) Face 完全保持真实深度；
//   2) 只把 CAD Edge 向相机方向移动极小的 depth quantum。
// 这样前表面仍然是遮挡基准，不会因为把整个 Face 后移而让内部/背面边在
// 掠射视角下穿出来。
inline void applyDefaultSurface(vtkProperty* property)
{
    if (!property)
        return;

    property->SetRepresentationToSurface();
    property->SetColor(0.78, 0.81, 0.85);
    property->SetOpacity(1.0);
    property->SetInterpolationToPhong();
    property->SetLighting(true);
    property->SetAmbient(0.34);
    property->SetDiffuse(0.64);
    property->SetSpecular(0.08);
    property->SetSpecularPower(18.0);
    property->EdgeVisibilityOff();
}

inline void applyDefaultEdge(vtkProperty* property)
{
    if (!property)
        return;

    property->SetRepresentationToWireframe();
    property->SetColor(0.10, 0.12, 0.15);
    property->SetOpacity(1.0);
    // FreeCAD 当前默认 Shape Line Width 为 2。这里直接采用同一默认值，
    // 不再通过 1.20px 之类的经验参数反复试错。
    property->SetLineWidth(2.0);
    property->SetLighting(false);
    property->SetAmbient(1.0);
    property->SetDiffuse(0.0);
    property->SetSpecular(0.0);

    // 不再依赖驱动对宽 GL_LINES 的光栅化实现。2px 斜线/曲线在 4x MSAA 下
    // 容易因为样本覆盖不同而出现同一条线一段黑、一段灰的视觉差异。
    // VTK 自带的 screen-space tube line 路径能保持宽度和覆盖更稳定，
    // 仍然是普通 vtkProperty 配置，不引入自定义 shader。
    property->SetRenderLinesAsTubes(1);
}

inline void applyPreviewSurface(vtkProperty* property, double opacity = 0.30)
{
    if (!property)
        return;

    property->SetRepresentationToSurface();
    property->SetColor(0.25, 0.76, 0.70);
    property->SetOpacity(opacity);
    property->SetInterpolationToPhong();
    property->SetLighting(true);
    property->SetAmbient(0.38);
    property->SetDiffuse(0.58);
    property->SetSpecular(0.05);
    property->SetSpecularPower(14.0);
}

inline void applyPreviewEdge(vtkProperty* property, double lineWidth = 1.35)
{
    if (!property)
        return;

    property->SetRepresentationToWireframe();
    property->SetColor(0.06, 0.47, 0.45);
    property->SetOpacity(0.98);
    property->SetLineWidth(lineWidth);
    property->SetLighting(false);
    property->SetAmbient(1.0);
    property->SetDiffuse(0.0);
    property->SetSpecular(0.0);
    property->SetRenderLinesAsTubes(1);
}

inline void applyReferenceGuide(vtkProperty* property, double lineWidth = 1.6,
                                double opacity = 0.88)
{
    if (!property)
        return;

    property->SetRepresentationToWireframe();
    property->SetColor(0.16, 0.53, 0.84);
    property->SetOpacity(opacity);
    property->SetLineWidth(lineWidth);
    property->SetLighting(false);
    property->SetAmbient(1.0);
    property->SetDiffuse(0.0);
    property->SetSpecular(0.0);
    property->SetRenderLinesAsTubes(0);
}

inline void configureDefaultSurfaceMapper(vtkPolyDataMapper* mapper)
{
    if (!mapper)
        return;

    // Face 不再做任何 depth bias。它必须保持真实深度，作为内部/背面 Edge
    // 的遮挡基准。上一版即使只后移几个 24-bit depth quantum，在极端掠射角
    // 下仍可能让非常靠近前表面的内部边通过深度测试。
    mapper->SetRelativeCoincidentTopologyPolygonOffsetParameters(0.0, 0.0);
    mapper->SetRelativeCoincidentTopologyLineOffsetParameters(0.0, 0.0);
}

inline void configureCoincidentEdgeMapper(vtkPolyDataMapper* mapper,
                                            double /*legacyLineUnits*/ = 0.0)
{
    if (!mapper)
        return;

    // v39：CAD Edge 已经进入独立的第二渲染层，Face 一定先写入深度，
    // Edge 再复用同一深度缓冲进行测试，因此不再靠“加大 z-bias”维持可见性。
    // 这里只保留约 2 个 24-bit depth quantum 的微小余量，用于跨过
    // line/polygon 光栅化的量化差；隐藏/背面 Edge 仍由第一层真实 Face 深度遮挡。
    constexpr double kVtkCoincidentScale = 65000.0;
    constexpr double kDepthLevels24 = 16777216.0;
    constexpr double kVisibleEdgeOffsetUnits =
        -2.0 * kVtkCoincidentScale / kDepthLevels24;

    mapper->SetRelativeCoincidentTopologyPolygonOffsetParameters(0.0, 0.0);
    mapper->SetRelativeCoincidentTopologyLineOffsetParameters(0.0, kVisibleEdgeOffsetUnits);
}

inline void configurePreviewSurfaceMapper(vtkPolyDataMapper* mapper)
{
    if (!mapper)
        return;
    mapper->SetRelativeCoincidentTopologyPolygonOffsetParameters(0.0, 0.0);
}
} // namespace GeometryDisplayStyle
