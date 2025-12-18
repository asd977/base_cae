#ifndef PUBLICCONSTANT_H
#define PUBLICCONSTANT_H

namespace PublicConstants {

// 软件
constexpr char kEventLoadComplete[] = "load_complete";
constexpr char kEventQuitApp[] = "quit_app";

// 工程
constexpr char kProjectDirectoryKey[] = "project_dir";
constexpr char kActionNewProject[] = "new_project";
constexpr char kPostProcessingProjectFileName[] = "postprocessing.json";
constexpr char kPreProcessingProjectFileName[] = "preprocessing.json";

// 前处理 / 后处理 action
constexpr char kActionPostProcessOccBoolean[] = "new_occboolen";
constexpr char kActionPostProcessNephogram[] = "new_nephogram";
constexpr char kActionPostProcessVector[] = "new_vector";
constexpr char kActionPostProcessSection[] = "new_section";
constexpr char kActionPostProcessDice[] = "new_dice";
constexpr char kActionPostProcessIsosurface[] = "new_isosurface";
constexpr char kActionPostProcessIsosurfaceSegmentation[] = "new_isosurfaceseg";
constexpr char kActionPostProcessXyPlot[] = "new_xyplot";
constexpr char kActionPostProcessAnimation[] = "new_animation";
constexpr char kActionPostProcessPickPhysical[] = "new_pickphysical";
constexpr char kActionPostProcessDerivative[] = "new_derivative";
constexpr char kActionPostProcessDataExport[] = "new_dataexp";

// 后处理 page
constexpr char kPagePostProcessNephogram[] = "nephogram";
constexpr char kPagePostProcessVector[] = "vector";
constexpr char kPagePostProcessSlice[] = "slice";
constexpr char kPagePostProcessBlock[] = "block";
constexpr char kPagePostProcessIsosurface[] = "isosurface";
constexpr char kPagePostProcessIsosurfaceSegmentation[] = "isosurfaceseg";
constexpr char kPagePostProcessAnimation[] = "animation";
constexpr char kPagePostProcessDerivative[] = "derivative";
constexpr char kPagePostProcessOccBoolean[] = "occboolen";

// 后处理中，当前使用的物理量名称
constexpr char kValuePostScalar[] = "post_scalar";

// 代理模型
constexpr char kActionSurrogateModelLatin[] = "new_LatinHypercubeSampling";
constexpr char kActionSurrogateModelOrthogonal[] = "new_OrthogonalSampling";
constexpr char kActionSurrogateModelDnn[] = "new_DNNModel";
constexpr char kActionSurrogateModelPod[] = "new_PODModel";
constexpr char kActionSurrogateModelDataEvaluation[] = "new_data_evaluation";

// 前处理 page
constexpr char kPagePreprocessArc[] = "arc";
constexpr char kPagePreprocessBezier2d[] = "bezier2d";
constexpr char kPagePreprocessBezier3d[] = "bezier3d";
constexpr char kPagePreprocessCone[] = "cone";
constexpr char kPagePreprocessCylinder[] = "cylinder";
constexpr char kPagePreprocessEllipse[] = "ellipse";
constexpr char kPagePreprocessImportGeometry[] = "importGeom";
constexpr char kPagePreprocessHelix[] = "helix";
constexpr char kPagePreprocessCircle[] = "circle";
constexpr char kPagePreprocessLine[] = "line";
constexpr char kPagePreprocessTorus[] = "torus";
constexpr char kPagePreprocessSquare[] = "square";
constexpr char kPagePreprocessSphere[] = "sphere";
constexpr char kPagePreprocessRectangle[] = "rectangle";
constexpr char kPagePreprocessPolygon[] = "polygon";
constexpr char kPagePreprocessMesh2d[] = "mesh2d";
constexpr char kPagePreprocessMesh3d[] = "mesh3d";
constexpr char kPagePreprocessImportGrid[] = "import_grid";
constexpr char kPagePreprocessInterCurve[] = "inter";
constexpr char kPagePreprocessPoint[] = "point";
constexpr char kPagePreprocessParametricCurve[] = "param";
constexpr char kPagePreprocessBox[] = "box";
constexpr char kPagePreprocessHyperbola[] = "hyper";
constexpr char kPagePreprocessParabola[] = "parabola";

// 边界条件
constexpr char kActionBoundarySolid[] = "boundary_solid";
constexpr char kActionBoundaryFace[] = "boundary_face";
constexpr char kActionBoundaryLine[] = "boundary_line";
constexpr char kActionBoundaryPoint[] = "boundary_point";

constexpr char kPageBoundarySolid[] = "page_boundary_solid";
constexpr char kPageBoundaryFace[] = "page_boundary_face";
constexpr char kPageBoundaryLine[] = "page_boundary_line";
constexpr char kPageBoundaryPoint[] = "page_boundary_point";

// 通知事件
constexpr char kUpdateGeomPointIndex[] = "update_geom_point_index";
constexpr char kUpdateGeomLineIndex[] = "update_geom_line_index";
constexpr char kUpdateGeomFaceIndex[] = "update_geom_face_index";
constexpr char kUpdateGeomSolidIndex[] = "update_geom_solid_index";
constexpr char kShowGeomSolidModel[] = "show_geom_solid_model";

constexpr char kSetGeomSelectType[] = "set_geom_select_type";
constexpr char kGetGridNodeIndex[] = "get_grid_node_index"; // 获取网格节点的集合
constexpr char kClearColorByGeomIndex[] = "clear_color_by_geom_index"; // 清除指定索引的颜色

}

#endif // PUBLICCONSTANT_H
