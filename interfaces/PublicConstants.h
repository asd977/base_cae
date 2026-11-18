#ifndef PUBLICCONSTANT_H
#define PUBLICCONSTANT_H

namespace PublicConstants
{

// =======================================================
// Event：发给 EventManager 的“事件名”（通知/状态变化/请求数据）
// =======================================================

// --- App lifecycle ---
constexpr char kEventLoadComplete[] = "load_complete";
constexpr char kEventQuitApp[] = "quit_app";
constexpr char kEventSolverFinished[] = "solver_finished";
constexpr char kEventUiThemeChanged[] = "ui_theme_changed";
constexpr char kEventBrandingVisibilityChanged[] = "branding_visibility_changed";

// --- Notify / Broadcast (几何、网格等状态通知) ---
constexpr char kUpdateGeomPointIndex[] = "update_geom_point_index";
constexpr char kUpdateGeomLineIndex[] = "update_geom_line_index";
constexpr char kUpdateGeomFaceIndex[] = "update_geom_face_index";
constexpr char kUpdateGeomSolidIndex[] = "update_geom_solid_index";
constexpr char kShowGeomSolidModel[] = "show_geom_solid_model";

// --- Request / Command-like event (一般带 data) ---
constexpr char kSetGeomSelectType[] = "set_geom_select_type";
constexpr char kGetGridNodeIndex[] = "get_grid_node_index";            // 获取网格节点集合
constexpr char kClearColorByGeomIndex[] = "clear_color_by_geom_index"; // 清除指定索引颜色

// =======================================================
// Action：用户点击/命令触发的“动作名”（通常由 UI QAction 触发）
// =======================================================

// --- Project / 工程 ---
constexpr char kActionNewProject[] = "new_project";
constexpr char kActionOpenProject[] = "open_project";
constexpr char kActionSaveProject[] = "save_project";
constexpr char kActionEXitProject[] = "exit_project";
constexpr char kActionExportCurrentDatabase[] = "export_current_database";

// --- PostProcess / 后处理 ---
constexpr char kActionPostProcessOccBoolean[] = "new_occboolen";
constexpr char kActionPostProcessNephogram[] = "new_nephogram";
constexpr char kActionPostProcessDisplayControl[] = "post_display_control";
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
constexpr char kActionPostProcessContourReader[] = "new_contour_reader";
constexpr char kActionPostProcessResultArrange[] = "new_result_arrange";
constexpr char kActionPostProcessResultRuleConfig[] = "post_result_rule_config";

// --- Surrogate Model / 代理模型 ---
constexpr char kActionSurrogateModelLatin[] = "new_LatinHypercubeSampling";
constexpr char kActionSurrogateModelOrthogonal[] = "new_OrthogonalSampling";
constexpr char kActionSurrogateModelDnn[] = "new_DNNModel";
constexpr char kActionSurrogateModelPod[] = "new_PODModel";
constexpr char kActionSurrogateModelDataEvaluation[] = "new_data_evaluation";

// --- Custom Param / 自定义参数（如果你 UI 有对应按钮） ---
constexpr char kActionCustomParam[] = "custom_param";
constexpr char kActionCustomFunction[] = "custom_function";

// =========================
// Preprocess / 几何
// =========================

// --- 导入导出（菜单仍在用） ---
constexpr char kActionPreprocessImportGeometry[] = "new_importGeom";
constexpr char kActionPreprocessExportGeometry[] = "new_exportGeom";

// --- 基本体3D ---
constexpr char kActionPreprocessBox[] = "new_box";
constexpr char kActionPreprocessSphere[] = "new_sphere";
constexpr char kActionPreprocessCone[] = "new_cone";
constexpr char kActionPreprocessTorus[] = "new_torus";
constexpr char kActionPreprocessCylinder[] = "new_cylinder";
constexpr char kActionPreprocessGravityDam[] = "new_gravity_dam";
constexpr char kActionPreprocessEarthRockfillDam[] = "new_earthrockfill_dam";
constexpr char kActionPreprocessSlopeTemplate[] = "new_slope_template";
constexpr char kActionPreprocessCavernTemplate[] = "new_cavern_template";

// --- 基本体2D  ---
constexpr char kActionGeomCreatePlanarRect[] = "geom_planar_rect";
constexpr char kActionGeomCreatePlanarCircle[] = "geom_planar_circle";
constexpr char kActionGeomCreateEllipse[] = "geom_planar_ellipse";
constexpr char kActionGeomCreatePlanarPolygon[] = "geom_planar_polygon";

// --- 布尔 ---
constexpr char kActionGeomFuse[] = "geom_boolean_fuse";
constexpr char kActionGeomUnion[] = "geom_boolean_union";
constexpr char kActionGeomCut[] = "geom_boolean_cut";
constexpr char kActionGeomCommon[] = "geom_boolean_common";

// --- 特征 Feature（菜单 actExtrude/Revolve/Thicken/Shell 用的） ---
constexpr char kActionGeomAddExtrude[] = "geom_feature_extrude";
constexpr char kActionGeomAddRevolve[] = "geom_feature_revolve";
constexpr char kActionGeomAddLoft[] = "geom_feature_loft";
constexpr char kActionGeomAddSweep[] = "geom_feature_sweep";
constexpr char kActionGeomAddCutExtrude[] = "geom_feature_cut_extrude";
constexpr char kActionGeomAddThicken[] = "geom_feature_thicken";
constexpr char kActionGeomAddShell[] = "geom_feature_shell";

// --- 修改 Modify（菜单 actEdgeBlend/Offset/Split 用的） ---
constexpr char kActionGeomAddEdgeBlend[] = "geom_modify_edgeblend";
constexpr char kActionGeomAddOffset[] = "geom_modify_offset";
constexpr char kActionGeomAddSplit[] = "geom_modify_split";
constexpr char kActionGeomAddChamfer[] = "geom_modify_chamfer";
constexpr char kActionGeomAddFillet[] = "geom_modify_fillet";

// --- 变换 Transform（菜单 actTransform/Mirror/Pattern 用的） ---
constexpr char kActionGeomAddTransform[] = "geom_transform";
constexpr char kActionGeomAddMirror[] = "geom_mirror";
constexpr char kActionGeomAddPattern[] = "geom_pattern";
constexpr char kActionGeomUndo[] = "geom_undo";
constexpr char kActionGeomRedo[] = "geom_redo";
constexpr char kActionGeomCheckRepair[] = "geom_check_repair";
constexpr char kActionGeomMeasure[] = "geom_measure";

// --- 草图 Sketch ---
constexpr char kActionGeomSketch2D[] = "geom_sketch_2d";
constexpr char kActionGeomTerrainSurface[] = "geom_terrain_surface";
constexpr char kActionSketchEnterEdit[] = "sketch_enter_edit";
constexpr char kActionSketchExitEdit[] = "sketch_exit_edit";
constexpr char kActionSketchToolLine[] = "sketch_tool_line";
constexpr char kActionSketchToolPolygon[] = "sketch_tool_polygon";
constexpr char kActionSketchToolPoint[] = "sketch_tool_point";
constexpr char kActionSketchToolRectangle[] = "sketch_tool_rectangle";
constexpr char kActionSketchToolCircle[] = "sketch_tool_circle";
constexpr char kActionSketchToolArc[] = "sketch_tool_arc";
constexpr char kActionSketchToolSpline[] = "sketch_tool_spline";
constexpr char kActionSketchConstraintCoincident[] = "sketch_constraint_coincident";
constexpr char kActionSketchConstraintHorizontal[] = "sketch_constraint_horizontal";
constexpr char kActionSketchConstraintVertical[] = "sketch_constraint_vertical";
constexpr char kActionSketchConstraintFixed[] = "sketch_constraint_fixed";
constexpr char kActionSketchConstraintEqual[] = "sketch_constraint_equal";
constexpr char kActionSketchSolve[] = "sketch_solve";
constexpr char kActionSketchGenerate[] = "sketch_generate";

// ---- Project / 文件 ----
// constexpr char kActionProjectOpen[]            = "proj_open";
// constexpr char kActionProjectSave[]            = "proj_save";
// constexpr char kActionProjectExportCurDB[]     = "proj_export_cur_db";
// constexpr char kActionProjectExit[]            = "proj_exit";

// ---- Preprocess / 网格 ----
constexpr char kActionPreprocessImportGrid[] = "grid_import";
constexpr char kActionPreprocessExportGrid[] = "grid_export";

constexpr char kActionPreprocessMeshGlobal[] = "grid_mesh_global"; // 整体剖分
constexpr char kActionPreprocessMeshSingle[] = "grid_mesh_single"; // 单体剖分
constexpr char kActionPreprocessMeshMapped[] = "grid_mesh_mapped"; // 映射剖分
constexpr char kActionPreprocessMeshSweep[] = "grid_mesh_sweep";   // 扫掠网格
constexpr char kActionPreprocessMeshPartition[] = "grid_mesh_partition"; // 扫掠分块
constexpr char kActionPreprocessMeshLocalFace[] = "grid_mesh_local_face";
constexpr char kActionPreprocessMeshLocalEdge[] = "grid_mesh_local_edge";

constexpr char kActionPreprocessMeshEdit[] = "mesh_ctrl_edit";       // 显示控制
constexpr char kActionPreprocessMeshDisplayPanel[] = "mesh_ctrl_display_panel"; // 仅打开显示控制面板，不切换当前结果/场景
constexpr char kActionPreprocessBarElements[] = "mesh_bar_elements"; // 杆单元布置
constexpr char kActionPreprocessMeshGrouping[] = "mesh_grouping";    // 网格分组
constexpr char kActionPreprocessQualityStd[] = "grid_quality_std";   // 网格质量标准

// --- Boundary / 边界条件 ---
constexpr char kActionBoundarySolid[] = "boundary_solid";
constexpr char kActionBoundaryFace[] = "boundary_face";
constexpr char kActionBoundaryLine[] = "boundary_line";
constexpr char kActionBoundaryPoint[] = "boundary_point";

// --- PhysicalField / 约束与荷载 ---
constexpr char kActionPhysicalDisplacementConstraint[] = "physical_displacement_constraint";
constexpr char kActionPhysicalGeoStressPoint[] = "physical_geo_stress_point";
constexpr char kActionPhysicalGeoStressFitFace[] = "physical_geo_stress_fit_face";
constexpr char kActionPhysicalBodyForce[] = "physical_body_force";
constexpr char kActionPhysicalConcentratedForce[] = "physical_concentrated_force";
constexpr char kActionPhysicalLinearForce[] = "physical_linear_force";
constexpr char kActionPhysicalSurfaceForce[] = "physical_surface_force";
constexpr char kActionPhysicalTemperatureLoad[] = "physical_temperature_load";
constexpr char kActionPhysicalBeamLoad[] = "physical_beam_load";
constexpr char kActionPhysicalSeismicAndAddedMass[] = "physical_seismic_and_added_mass";

// --- View / 显示（你补齐的动作） ---
constexpr char kActionViewPosX[] = "view_pos_x";
constexpr char kActionViewNegX[] = "view_neg_x";
constexpr char kActionViewPosY[] = "view_pos_y";
constexpr char kActionViewNegY[] = "view_neg_y";
constexpr char kActionViewPosZ[] = "view_pos_z";
constexpr char kActionViewNegZ[] = "view_neg_z";
constexpr char kActionViewResetCamera[] = "view_reset_camera";
constexpr char kActionViewResetLayout[] = "view_reset_layout";
constexpr char kActionViewToggleStaff[] = "view_toggle_staff";
constexpr char kActionViewToggleLight[] = "view_toggle_light";
constexpr char kActionViewSetTransparency[] = "view_set_transparency";
constexpr char kActionViewSetTheme[] = "view_set_theme";

// =======================================================
// Page：页面/节点的“页面ID”（工程树/属性面板切换用）
// =======================================================

// --- PostProcess pages ---
constexpr char kPagePostProcessNephogram[] = "nephogram";
constexpr char kPagePostProcessVector[] = "vector";
constexpr char kPagePostProcessSlice[] = "slice";
constexpr char kPagePostProcessBlock[] = "block";
constexpr char kPagePostProcessIsosurface[] = "isosurface";
constexpr char kPagePostProcessIsosurfaceSegmentation[] = "isosurfaceseg";
constexpr char kPagePostProcessAnimation[] = "animation";
constexpr char kPagePostProcessDerivative[] = "derivative";
constexpr char kPagePostProcessCurve[] = "curve";
constexpr char kPagePostProcessContourReader[] = "contour_reader";
constexpr char kPagePostProcessOccBoolean[] = "occboolen";
constexpr char kPagePostProcessResultArrange[] = "result_arrange";

// --- PreProcess pages ---
constexpr char kPagePreprocessImportGeometry[] = "importGeom";
constexpr char kPagePreprocessImportGrid[] = "import_grid";

constexpr char kPagePreprocessCircle[] = "circle";
constexpr char kPagePreprocessSquare[] = "square";
constexpr char kPagePreprocessEllipse[] = "ellipse";
constexpr char kPagePreprocessRectangle[] = "rectangle";
constexpr char kPagePreprocessPolygon[] = "polygon";
constexpr char kPagePreprocessPoint[] = "point";

constexpr char kPagePreprocessArc[] = "arc";
constexpr char kPagePreprocessLine[] = "line";
constexpr char kPagePreprocessInterCurve[] = "inter";
constexpr char kPagePreprocessParametricCurve[] = "param";
constexpr char kPagePreprocessBezier2d[] = "bezier2d";
constexpr char kPagePreprocessBezier3d[] = "bezier3d";
constexpr char kPagePreprocessParabola[] = "parabola";
constexpr char kPagePreprocessHyperbola[] = "hyper";

constexpr char kPagePreprocessBox[] = "box";
constexpr char kPagePreprocessSphere[] = "sphere";
constexpr char kPagePreprocessCone[] = "cone";
constexpr char kPagePreprocessCylinder[] = "cylinder";
constexpr char kPagePreprocessTorus[] = "torus";
constexpr char kPagePreprocessGravityDam[] = "gravity_dam";
constexpr char kPagePreprocessEarthRockfillDam[] = "earthrockfill_dam";
constexpr char kPagePreprocessSlopeTemplate[] = "slope_template";
constexpr char kPagePreprocessCavernTemplate[] = "cavern_template";
constexpr char kPagePreprocessHelix[] = "helix";

constexpr char kPageGeomExtrude[] = "extrude";
constexpr char kPageGeomRevolve[] = "revolve";
constexpr char kPageGeomLoft[] = "loft";
constexpr char kPageGeomSweep[] = "sweep";
constexpr char kPageGeomPattern[] = "pattern";
constexpr char kPageGeomTransform[] = "transform";
constexpr char kPageGeomMirror[] = "mirror";
constexpr char kPageGeomSplit[] = "split";
constexpr char kPageGeomChamfer[] = "chamfer";
constexpr char kPageGeomFillet[] = "fillet";
constexpr char kPageGeomSketch2D[] = "sketch2d";
constexpr char kPageGeomTerrainSurface[] = "terrain_surface";

constexpr char kPagePreprocessMesh2d[] = "mesh2d";
constexpr char kPagePreprocessMesh3d[] = "mesh3d";

// --- Boundary pages ---
constexpr char kPageBoundarySolid[] = "page_boundary_solid";
constexpr char kPageBoundaryFace[] = "page_boundary_face";
constexpr char kPageBoundaryLine[] = "page_boundary_line";
constexpr char kPageBoundaryPoint[] = "page_boundary_point";

// =========================
// Solver / 分析类型
// =========================
constexpr char kActionSeepageKey[] = "seepage_analysis";
constexpr char kActionStressStrainKey[] = "stress_strain_analysis";
constexpr char kActionSlopeStabilityKey[] = "slope_stability_analysis";
constexpr char kActionCaveExcavationKey[] = "cave_excavation_analysis";
constexpr char kActionCaveBlockStabilityKey[] = "cave_block_stability_analysis";
constexpr char kActionSpecificationCheckKey[] = "specification_check_analysis";

// =======================================================
// Other：Key / Value / FileName（不属于 Event/Action/Page）
// =======================================================

// --- Project keys / files ---
constexpr char kProjectDirectoryKey[] = "project_dir";
constexpr char kSolverRunDirectoryKey[] = "solver_run_dir";
constexpr char kProjectDataDirectoryName[] = u8"工程数据";
constexpr char kProjectInputDirectoryName[] = u8"工程数据/计算输入";
constexpr char kAnalysisInputDirectoryName[] = u8"计算输入";
constexpr char kLegacyInputDirectoryName[] = "PREP";
constexpr char kPostProcessingProjectFileName[] = u8"工程数据/postprocessing.json";
constexpr char kPreProcessingProjectFileName[] = u8"工程数据/preprocessing.json";
constexpr char kMeshProjectFileName[] = u8"工程数据/mesh.json";
constexpr char kMaterialProjectFileName[] = u8"工程数据/material.json";
constexpr char kPhysicalFieldProjectFileName[] = u8"工程数据/physical_field.json";
constexpr char kMeshSnapshotFileName[] = u8"工程数据/mesh_snapshot.iiin.dat";
constexpr char kMeshMapFileName[] = u8"工程数据/mesh_map.json";
constexpr char kSolverProjectFileName[] = u8"工程数据/solver.json";
constexpr char kSurrogateProjectFileName[] = u8"工程数据/surrogate_model.json";
constexpr char kSolverAnalysisManifestFileName[] = u8"分析信息.json";
constexpr char kPostResultDirectoryName[] = u8"计算结果";

// --- Runtime values ---
constexpr char kValuePostScalar[] = "post_scalar";

// --- 网格类型：几维空间 ---
constexpr char kMeshDimension[] = "mesh_dimension";

} // namespace PublicConstants

#endif // PUBLICCONSTANT_H
