#ifndef PUBLICCONSTANT_H
#define PUBLICCONSTANT_H


namespace PublicConstants {

    //软件
    const char EVENT_LODA_COMPLETE[] = "load_complete";
    const char EVENT_QUIT_APP[] = "quit_app";

    //工程
    const char VALUE_PROJECT_DIR[] = "project_dir";
    const char ACTION_NEW_PROJECT[] = "new_project";
    const char PROJECT_FILE_POSTPROCESSING[] = "postprocessing.json";
	const char PROJECT_FILE_PREPROCESSING[] = "preprocessing.json";
//    const char ACTION_NEW_PROJECT[] = "new_project";
//    const char ACTION_NEW_PROJECT[] = "new_project";
//    const char ACTION_NEW_PROJECT[] = "new_project";
//    const char ACTION_NEW_PROJECT[] = "new_project";
//    const char ACTION_NEW_PROJECT[] = "new_project";

    //前处理-action
    const char ACTION_POSTPROCESS_OCCBOOLEN[] = "new_occboolen";

    //后处理-action
    const char ACTION_POSTPROCESS_NEPHOGRAM[] = "new_nephogram";
    const char ACTION_POSTPROCESS_VECTOR[] = "new_vector";
    const char ACTION_POSTPROCESS_SECTION[] = "new_section";
    const char ACTION_POSTPROCESS_DICE[] = "new_dice";
    const char ACTION_POSTPROCESS_ISOSURFACE[] = "new_isosurface";
    const char ACTION_POSTPROCESS_ISOSURFACESEG[] = "new_isosurfaceseg";
    const char ACTION_POSTPROCESS_XYPLOT[] = "new_xyplot";
    const char ACTION_POSTPROCESS_ANIMATION[] = "new_animation";
    const char ACTION_POSTPROCESS_PICKPHYSICAL[] = "new_pickphysical";
    const char ACTION_POSTPROCESS_DERIVATIVE[] = "new_derivative";
    const char ACTION_POSTPROCESS_DATAEXP[] = "new_dataexp";
    //后处理-page
    const char PAGE_POSTPROCESS_NEPHOGRAM[] = "nephogram";
    const char PAGE_POSTPROCESS_VECTOR[] = "vector";
    const char PAGE_POSTPROCESS_SLICE[] = "slice";
    const char PAGE_POSTPROCESS_BLOCK[] = "block";
    const char PAGE_POSTPROCESS_ISOSURFACE[] = "isosurface";
    const char PAGE_POSTPROCESS_ISOSURFACESEG[] = "isosurfaceseg";
//    const char ACTION_POSTPROCESS_XYPLOT[] = "xyplot";
    const char PAGE_POSTPROCESS_ANIMATION[] = "animation";
//    const char ACTION_POSTPROCESS_PICKPHYSICAL[] = "pickphysical";
    const char PAGE_POSTPROCESS_DERIVATIVE[] = "derivative";
//    const char ACTION_POSTPROCESS_DATAEXP[] = "dataexp";

    //后处理中，当前使用的物理量名称
    const char VALUE_POST_SCALAR[] = "post_scalar";

    //代理模型
    const char ACTION_SURROGATEMODEL_LATIN[] = "new_LatinHypercubeSampling";
    const char ACTION_SURROGATEMODEL_ORTHOGONAL[] = "new_OrthogonalSampling";
    const char ACTION_SURROGATEMODEL_DNN[] = "new_DNNModel";
    const char ACTION_SURROGATEMODEL_POD[] = "new_PODModel";
    const char ACTION_SURROGATEMODEL_DATAEVALUATION[] = "new_data_evaluation";

	//前处理
	const char PAGE_PREPROCESS_ARC[] = "arc";
	const char PAGE_PREPROCESS_BEZIER2D[] = "bezier2d";
	const char PAGE_PREPROCESS_BEZIER3D[] = "bezier3d";
	const char PAGE_PREPROCESS_CONE[] = "cone";
	const char PAGE_PREPROCESS_CYLINDER[] = "cylinder";
	const char PAGE_PREPROCESS_ELLIPSE[] = "ellipse";
	const char PAGE_PREPROCESS_IMPORTGEOM[] = "importGeom";
	const char PAGE_PREPROCESS_HELIX[] = "helix";
	const char PAGE_PREPROCESS_CIRCLE[] = "circle";
	const char PAGE_PREPROCESS_LINE[] = "line";
	const char PAGE_PREPROCESS_TORUS[] = "torus";
	const char PAGE_PREPROCESS_SQUARE[] = "square";
	const char PAGE_PREPROCESS_SPHERE[] = "sphere";
	const char PAGE_PREPROCESS_RECTANGLE[] = "rectangle";
	const char PAGE_PREPROCESS_POLYGON[] = "polygon";
	const char PAGE_PREPROCESS_MESH2D[] = "mesh2d";
	const char PAGE_PREPROCESS_MESH3D[] = "mesh3d";
	const char PAGE_PREPROCESS_IMPORT_GRID[] = "import_grid";
	const char PAGE_PREPROCESS_INTER[] = "inter";
	const char PAGE_PREPROCESS_POINT[] = "point";
	const char PAGE_PREPROCESS_PARAM[] = "param";
	const char PAGE_PREPROCESS_BOX[] = "box";
	const char PAGE_PREPROCESS_HYPER[] = "hyper";
	const char PAGE_PREPROCESS_PARABOLA[] = "parabola";
    //处理-page
    const char PAGE_POSTPROCESS_OCCBOOLEN[] = "occboolen";


	// 边界条件
	const char ACTION_BOUNDARY_SOLID[] = "boundary_solid";
	const char ACTION_BOUNDARY_FACE[] = "boundary_face";
	const char ACTION_BOUNDARY_LINE[] = "boundary_line";
	const char ACTION_BOUNDARY_POINT[] = "boundary_point";

	const char PAGE_BOUNDARY_SOLID[] = "page_boundary_solid";
	const char PAGE_BOUNDARY_FACE[] = "page_boundary_face";
	const char PAGE_BOUNDARY_LINE[] = "page_boundary_line";
	const char PAGE_BOUNDARY_POINT[] = "page_boundary_point";

	
	const char UPDATE_GEOM_POINT_INDEX[] = "update_geom_point_index";
	const char UPDATE_GEOM_LINE_INDEX[] = "update_geom_line_index";
	const char UPDATE_GEOM_FACE_INDEX[] = "update_geom_face_index";
	const char UPDATE_GEOM_SOLID_INDEX[] = "update_geom_solid_index";
	const char SHOW_GEOM_SOLID_MODEL[] = "show_geom_solid_model";

	const char SET_GEOM_SELECT_TYPE[] = "set_geom_select_type";
	const char GET_GRID_NODE_INDEX[] = "get_grid_node_index";// 获取网格节点的集合
	const char CLEAR_COLOR_BY_GEOM_INDEX[] = "clear_color_by_geom_index";// 获取网格节点的集合

	


	
	
	


	

	
	

	

	
	
	

	
	

	

	
	
	
	
	

	
}

#endif // PUBLICCONSTANT_H
