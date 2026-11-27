add_executable(SmeshMeshingChecks
    SmeshMeshingChecks.cpp
    ../plugins/Base_Mesh/SmeshMeshGenerator.cpp)
add_executable(GeometryProjectStorageChecks GeometryProjectStorageChecks.cpp)
add_executable(WorkingPathsChecks WorkingPathsChecks.cpp)
target_include_directories(WorkingPathsChecks PRIVATE "${CMAKE_SOURCE_DIR}/interfaces" "${CMAKE_SOURCE_DIR}/core")
target_link_libraries(WorkingPathsChecks PRIVATE core Qt6::Core CAE::VTK)
cae_vtk_autoinit(WorkingPathsChecks)
add_test(NAME WorkingPathsChecks COMMAND WorkingPathsChecks)
target_include_directories(GeometryProjectStorageChecks PRIVATE "${CMAKE_SOURCE_DIR}/interfaces")
target_link_libraries(GeometryProjectStorageChecks PRIVATE Qt6::Core CAE::OCC)
add_test(NAME GeometryProjectStorageChecks COMMAND GeometryProjectStorageChecks)
target_include_directories(SmeshMeshingChecks PRIVATE "${CMAKE_SOURCE_DIR}/interfaces")
target_link_libraries(SmeshMeshingChecks PRIVATE Qt6::Core Qt6::Gui CAE::OCC CAE::VTK)
add_dependencies(SmeshMeshingChecks CAE_SMESHBridge)
cae_vtk_autoinit(SmeshMeshingChecks)
add_test(NAME SmeshMeshingChecks COMMAND SmeshMeshingChecks)
string(REPLACE ";" "\\;" _smesh_test_path "${CMAKE_BINARY_DIR}/plugins;${CMAKE_BINARY_DIR};${CAE_QT_ROOT}/bin;$ENV{PATH}")
set_tests_properties(WorkingPathsChecks GeometryProjectStorageChecks PROPERTIES ENVIRONMENT "PATH=${_smesh_test_path}")
set_tests_properties(SmeshMeshingChecks PROPERTIES TIMEOUT 180
    ENVIRONMENT "PATH=${_smesh_test_path}")

add_executable(SmeshWorkflowChecks SmeshWorkflowChecks.cpp ../plugins/Base_Mesh/SmeshMeshGenerator.cpp)
add_executable(GmdsPartitionChecks GmdsPartitionChecks.cpp ../plugins/Base_Mesh/SmeshMeshGenerator.cpp)
target_include_directories(GmdsPartitionChecks PRIVATE "${CMAKE_SOURCE_DIR}/interfaces")
target_link_libraries(GmdsPartitionChecks PRIVATE core Qt6::Core Qt6::Gui CAE::OCC CAE::VTK)
add_dependencies(GmdsPartitionChecks CAE_GMDSWorker CAE_SMESHBridge)
cae_vtk_autoinit(GmdsPartitionChecks)
add_test(NAME GmdsPartitionChecks COMMAND GmdsPartitionChecks)
set_tests_properties(GmdsPartitionChecks PROPERTIES TIMEOUT 60 ENVIRONMENT "PATH=${_smesh_test_path}")
add_executable(GmdsConformalHybridChecks GmdsConformalHybridChecks.cpp)
target_include_directories(GmdsConformalHybridChecks PRIVATE "${CMAKE_SOURCE_DIR}/interfaces")
target_link_libraries(GmdsConformalHybridChecks PRIVATE core Qt6::Core CAE::OCC CAE::VTK)
add_dependencies(GmdsConformalHybridChecks CAE_GMDSWorker CAE_SMESHBridge)
cae_vtk_autoinit(GmdsConformalHybridChecks)
add_test(NAME GmdsConformalHybridChecks COMMAND GmdsConformalHybridChecks)
set_tests_properties(GmdsConformalHybridChecks PROPERTIES TIMEOUT 60 ENVIRONMENT "PATH=${_smesh_test_path}")
target_include_directories(SmeshWorkflowChecks PRIVATE "${CMAKE_SOURCE_DIR}/interfaces")
target_link_libraries(SmeshWorkflowChecks PRIVATE Qt6::Core Qt6::Gui CAE::OCC CAE::VTK)
add_dependencies(SmeshWorkflowChecks CAE_SMESHBridge)
cae_vtk_autoinit(SmeshWorkflowChecks)
add_test(NAME SmeshWorkflowChecks COMMAND SmeshWorkflowChecks)
set_tests_properties(SmeshWorkflowChecks PROPERTIES TIMEOUT 180
    ENVIRONMENT "PATH=${_smesh_test_path}")

add_executable(SmeshGeometryScopeChecks SmeshGeometryScopeChecks.cpp ../plugins/Base_Mesh/SmeshMeshGenerator.cpp)
target_include_directories(SmeshGeometryScopeChecks PRIVATE "${CMAKE_SOURCE_DIR}/interfaces")
target_link_libraries(SmeshGeometryScopeChecks PRIVATE Qt6::Core Qt6::Gui CAE::OCC CAE::VTK)
add_dependencies(SmeshGeometryScopeChecks CAE_SMESHBridge)
cae_vtk_autoinit(SmeshGeometryScopeChecks)
add_test(NAME SmeshGeometryScopeChecks COMMAND SmeshGeometryScopeChecks)
set_tests_properties(SmeshGeometryScopeChecks PROPERTIES TIMEOUT 180 ENVIRONMENT "PATH=${_smesh_test_path}")

add_executable(SolverRegressionDriver SolverRegressionDriver.cpp)
target_include_directories(SolverRegressionDriver PRIVATE "${CMAKE_SOURCE_DIR}/interfaces" "${CMAKE_SOURCE_DIR}/core")
target_link_libraries(SolverRegressionDriver PRIVATE core Qt6::Widgets CAE::VTK)
cae_vtk_autoinit(SolverRegressionDriver)

add_executable(SolverDirectoryProtocolChecks SolverDirectoryProtocolChecks.cpp)
target_link_libraries(SolverDirectoryProtocolChecks PRIVATE Qt6::Core)
add_test(NAME SolverDirectoryProtocolChecks COMMAND SolverDirectoryProtocolChecks)

add_executable(CaveExcavationProtocolChecks CaveExcavationProtocolChecks.cpp)
target_include_directories(CaveExcavationProtocolChecks PRIVATE "${CMAKE_SOURCE_DIR}")
target_link_libraries(CaveExcavationProtocolChecks PRIVATE Qt6::Core)
add_test(NAME CaveExcavationProtocolChecks COMMAND CaveExcavationProtocolChecks)
add_executable(DamInputProtocolChecks DamInputProtocolChecks.cpp)
target_link_libraries(DamInputProtocolChecks PRIVATE Qt6::Core)
add_test(NAME DamInputProtocolChecks COMMAND DamInputProtocolChecks)
add_executable(ThermalLoadProtocolChecks ThermalLoadProtocolChecks.cpp)
target_link_libraries(ThermalLoadProtocolChecks PRIVATE Qt6::Core)
add_test(NAME ThermalLoadProtocolChecks COMMAND ThermalLoadProtocolChecks)
add_executable(SolverProcessFixture SolverProcessFixture.cpp)
target_link_libraries(SolverProcessFixture PRIVATE Qt6::Core)

add_executable(OutputLoggingChecks OutputLoggingChecks.cpp
    ../plugins/Base_Output/Output.cpp ../plugins/Base_Output/OutputWidget.cpp
    ../plugins/Base_Output/Output.h ../plugins/Base_Output/OutputWidget.h
    ../plugins/Base_Output/OutputWidget.ui)
target_link_libraries(OutputLoggingChecks PRIVATE core Qt6::Widgets)
add_test(NAME OutputLoggingChecks COMMAND OutputLoggingChecks)
add_executable(SolverErrorLogChecks SolverErrorLogChecks.cpp)
add_test(NAME SolverErrorLogChecks COMMAND SolverErrorLogChecks)

add_executable(PostResultFunChecks PostResultFunChecks.cpp
    ../plugins/Base_PostProcessing/PostResultGenerator.cpp
    ../plugins/Base_PostProcessing/PostResultAutoPrepare.cpp
    ../plugins/Base_PostProcessing/PostTecplotUtils.cpp)
target_include_directories(PostResultFunChecks PRIVATE "${CMAKE_SOURCE_DIR}/core" "${CMAKE_SOURCE_DIR}/interfaces")
target_link_libraries(PostResultFunChecks PRIVATE Qt6::Widgets CAE::VTK)
cae_vtk_autoinit(PostResultFunChecks)
add_test(NAME PostResultFunChecks COMMAND PostResultFunChecks)
set_tests_properties(PostResultFunChecks PROPERTIES ENVIRONMENT "PATH=${_smesh_test_path}")

add_executable(PostSurfacePipelineChecks PostSurfacePipelineChecks.cpp
    ../plugins/Base_PostProcessing/PostSurfacePipeline.cpp
    ../plugins/Base_PostProcessing/PostTecplotUtils.cpp)
target_include_directories(PostSurfacePipelineChecks PRIVATE "${CMAKE_SOURCE_DIR}/core" "${CMAKE_SOURCE_DIR}/interfaces")
target_link_libraries(PostSurfacePipelineChecks PRIVATE Qt6::Widgets CAE::VTK)
cae_vtk_autoinit(PostSurfacePipelineChecks)
add_test(NAME PostSurfacePipelineChecks COMMAND PostSurfacePipelineChecks)
set_tests_properties(PostSurfacePipelineChecks PROPERTIES TIMEOUT 120 ENVIRONMENT "PATH=${_smesh_test_path}")

add_executable(UiGeometryPostChecks UiGeometryPostChecks.cpp ../res.qrc
    ../plugins/Base_PostProcessing/PostTecplotUtils.cpp
    ../plugins/Base_Gemo/sketch/SketchCurveTools.cpp
    ../plugins/Base_Gemo/sketch/SketchDocument.cpp
    ../plugins/Base_Gemo/sketch/SketchShapeBuilder.cpp
    ../plugins/Base_Gemo/sketch/SketchPlanarRegionBuilder.cpp
    ../plugins/Base_PostProcessing/PostDisplayControlWidget.cpp
    ../plugins/Base_PostProcessing/PostDisplayControlWidget.h
    ../plugins/Base_PostProcessing/PostDisplayControlWidget.ui)
target_include_directories(UiGeometryPostChecks PRIVATE "${CMAKE_SOURCE_DIR}/core" "${CMAKE_SOURCE_DIR}/interfaces")
target_link_libraries(UiGeometryPostChecks PRIVATE core Qt6::Widgets CAE::OCC CAE::VTK)
cae_vtk_autoinit(UiGeometryPostChecks)
add_test(NAME UiGeometryPostChecks COMMAND UiGeometryPostChecks)
set_tests_properties(UiGeometryPostChecks PROPERTIES TIMEOUT 120 ENVIRONMENT "PATH=${_smesh_test_path};QT_QPA_PLATFORM=windows")
add_dependencies(UiGeometryPostChecks Base_PostProcessing Base_UiFrame Base_Visualization Base_ProjectManager Base_Output Base_PhysicalField)
add_dependencies(UiGeometryPostChecks Base_GlobalParameter Base_MaterialManager Base_Solver Base_Gemo Base_Mesh)
add_test(NAME CloudLightingChecks COMMAND UiGeometryPostChecks "${CMAKE_BINARY_DIR}/test-evidence/native-cloud" --cloud)
set_tests_properties(CloudLightingChecks PROPERTIES TIMEOUT 120 ENVIRONMENT "PATH=${_smesh_test_path};QT_QPA_PLATFORM=windows")
add_test(NAME SceneVisibilityChecks COMMAND UiGeometryPostChecks "${CMAKE_BINARY_DIR}/test-evidence/scene-visibility" --scene-visibility)
set_tests_properties(SceneVisibilityChecks PROPERTIES TIMEOUT 120 ENVIRONMENT "PATH=${_smesh_test_path};QT_QPA_PLATFORM=windows")
add_test(NAME MeshDomainRebuildChecks COMMAND UiGeometryPostChecks "${CMAKE_BINARY_DIR}/test-evidence/mesh-domains" --mesh-domains)
set_tests_properties(MeshDomainRebuildChecks PROPERTIES TIMEOUT 120 ENVIRONMENT "PATH=${_smesh_test_path};QT_QPA_PLATFORM=windows")
add_test(NAME MaterialTensileStrengthChecks COMMAND UiGeometryPostChecks "${CMAKE_BINARY_DIR}/test-evidence/material-tensile" --material-tensile)
set_tests_properties(MaterialTensileStrengthChecks PROPERTIES TIMEOUT 60 ENVIRONMENT "PATH=${_smesh_test_path};QT_QPA_PLATFORM=windows")
add_test(NAME SettingsPageLayoutChecks COMMAND UiGeometryPostChecks "${CMAKE_BINARY_DIR}/test-evidence/settings-layout" --layout-audit)
set_tests_properties(SettingsPageLayoutChecks PROPERTIES TIMEOUT 120 ENVIRONMENT "PATH=${_smesh_test_path};QT_QPA_PLATFORM=windows")
add_test(NAME MainWindowLayoutChecks COMMAND UiGeometryPostChecks "${CMAKE_BINARY_DIR}/test-evidence/main-window-layout")
set_tests_properties(MainWindowLayoutChecks PROPERTIES TIMEOUT 120 ENVIRONMENT "PATH=${_smesh_test_path};QT_QPA_PLATFORM=windows")
add_executable(SettingsLayoutChecks SettingsLayoutChecks.cpp)
target_link_libraries(SettingsLayoutChecks PRIVATE core Qt6::Widgets)
add_test(NAME SettingsLayoutChecks COMMAND SettingsLayoutChecks)
set_tests_properties(SettingsLayoutChecks PROPERTIES TIMEOUT 30 ENVIRONMENT "PATH=${_smesh_test_path};QT_QPA_PLATFORM=windows")
add_executable(NumericDisplayChecks NumericDisplayChecks.cpp)
target_include_directories(NumericDisplayChecks PRIVATE "${CMAKE_SOURCE_DIR}/core")
target_link_libraries(NumericDisplayChecks PRIVATE Qt6::Widgets)
add_test(NAME NumericDisplayChecks COMMAND NumericDisplayChecks)
set_tests_properties(NumericDisplayChecks PROPERTIES TIMEOUT 30 ENVIRONMENT "PATH=${_smesh_test_path};QT_QPA_PLATFORM=windows")
add_executable(PostMeshEdgeChecks PostMeshEdgeChecks.cpp)
target_include_directories(PostMeshEdgeChecks PRIVATE "${CMAKE_SOURCE_DIR}/core" "${CMAKE_SOURCE_DIR}/interfaces")
target_link_libraries(PostMeshEdgeChecks PRIVATE core Qt6::Widgets CAE::VTK)
cae_vtk_autoinit(PostMeshEdgeChecks)
add_test(NAME PostMeshEdgeChecks COMMAND PostMeshEdgeChecks)
set_tests_properties(PostMeshEdgeChecks PROPERTIES TIMEOUT 60 ENVIRONMENT "PATH=${_smesh_test_path};QT_QPA_PLATFORM=windows")

add_executable(VectorDisplayChecks VectorDisplayChecks.cpp
    ../plugins/Base_PostProcessing/VectorSettingWidget.cpp
    ../plugins/Base_PostProcessing/VectorSettingWidget.h
    ../plugins/Base_PostProcessing/VectorSettingWidget.ui
    ../plugins/Base_PostProcessing/PostResultGenerator.cpp
    ../plugins/Base_PostProcessing/PostResultAutoPrepare.cpp
    ../plugins/Base_PostProcessing/PostTecplotUtils.cpp)
target_include_directories(VectorDisplayChecks PRIVATE "${CMAKE_SOURCE_DIR}/core" "${CMAKE_SOURCE_DIR}/interfaces")
target_link_libraries(VectorDisplayChecks PRIVATE core Qt6::Widgets CAE::VTK)
cae_vtk_autoinit(VectorDisplayChecks)
add_test(NAME VectorDisplayChecks COMMAND VectorDisplayChecks)
set_tests_properties(VectorDisplayChecks PROPERTIES TIMEOUT 120 ENVIRONMENT "PATH=${_smesh_test_path}")

add_executable(PostRefreshChecks PostRefreshChecks.cpp
    ../plugins/Base_PostProcessing/BlockSettingsWidget.cpp
    ../plugins/Base_PostProcessing/SliceSettingsWidget.cpp
    ../plugins/Base_PostProcessing/IsosurfaceSettingsWidget.cpp
    ../plugins/Base_PostProcessing/IsosurfaceSegmentationWidget.cpp
    ../plugins/Base_PostProcessing/PostResultGenerator.cpp
    ../plugins/Base_PostProcessing/PostResultAutoPrepare.cpp
    ../plugins/Base_PostProcessing/PostTecplotUtils.cpp)
target_include_directories(PostRefreshChecks PRIVATE "${CMAKE_SOURCE_DIR}/core" "${CMAKE_SOURCE_DIR}/interfaces")
target_link_libraries(PostRefreshChecks PRIVATE core Qt6::Widgets CAE::VTK)
cae_vtk_autoinit(PostRefreshChecks)
add_test(NAME PostRefreshChecks COMMAND PostRefreshChecks)
set_tests_properties(PostRefreshChecks PROPERTIES TIMEOUT 120 ENVIRONMENT "PATH=${_smesh_test_path}")

add_executable(SlopeExcavationChecks SlopeExcavationChecks.cpp)
add_executable(CaveReinforcementFieldChecks CaveReinforcementFieldChecks.cpp
    ../plugins/Base_Solver/CaveReinforcementFieldPage.cpp
    ../plugins/Base_Solver/CaveReinforcementFieldPage.h)
target_include_directories(CaveReinforcementFieldChecks PRIVATE "${CMAKE_SOURCE_DIR}/core" "${CMAKE_SOURCE_DIR}/interfaces")
target_link_libraries(CaveReinforcementFieldChecks PRIVATE core Qt6::Widgets CAE::VTK)
cae_vtk_autoinit(CaveReinforcementFieldChecks)
add_test(NAME CaveReinforcementFieldChecks COMMAND CaveReinforcementFieldChecks)
set_tests_properties(CaveReinforcementFieldChecks PROPERTIES TIMEOUT 60 ENVIRONMENT "PATH=${_smesh_test_path};QT_QPA_PLATFORM=windows")
add_executable(SlopeSearchChecks SlopeSearchChecks.cpp
    ../plugins/Base_Solver/SlopeStandardDatModel.cpp)
target_link_libraries(SlopeSearchChecks PRIVATE Qt6::Core)
add_test(NAME SlopeSearchChecks COMMAND SlopeSearchChecks)
set_tests_properties(SlopeSearchChecks PROPERTIES ENVIRONMENT "PATH=${_smesh_test_path}")
add_executable(SlopeSectionDisplayChecks SlopeSectionDisplayChecks.cpp)
target_link_libraries(SlopeSectionDisplayChecks PRIVATE CAE::VTK)
cae_vtk_autoinit(SlopeSectionDisplayChecks)
add_test(NAME SlopeSectionDisplayChecks COMMAND SlopeSectionDisplayChecks)
set_tests_properties(SlopeSectionDisplayChecks PROPERTIES ENVIRONMENT "PATH=${_smesh_test_path}")
target_include_directories(SlopeExcavationChecks PRIVATE "${CMAKE_SOURCE_DIR}/core" "${CMAKE_SOURCE_DIR}/interfaces")
target_link_libraries(SlopeExcavationChecks PRIVATE core Qt6::Widgets CAE::VTK)
cae_vtk_autoinit(SlopeExcavationChecks)
add_test(NAME SlopeExcavationChecks COMMAND SlopeExcavationChecks)
set_tests_properties(SlopeExcavationChecks PROPERTIES ENVIRONMENT "PATH=${_smesh_test_path}")

add_executable(NastranImportChecks NastranImportChecks.cpp ../plugins/Base_Mesh/MeshGridIO.cpp)
target_include_directories(NastranImportChecks PRIVATE "${CMAKE_SOURCE_DIR}/core" "${CMAKE_SOURCE_DIR}/interfaces")
target_link_libraries(NastranImportChecks PRIVATE core Qt6::Widgets CAE::VTK)
cae_vtk_autoinit(NastranImportChecks)
add_test(NAME NastranImportChecks COMMAND NastranImportChecks)
set_tests_properties(NastranImportChecks PROPERTIES ENVIRONMENT "PATH=${_smesh_test_path}")

add_executable(GdalTerrainChecks GdalTerrainChecks.cpp
    ../plugins/Base_Gemo/builders/DxfReader.cpp
    ../plugins/Base_Gemo/terrain/TerrainSurfaceBuilder.cpp)
target_include_directories(GdalTerrainChecks PRIVATE
    "${CMAKE_SOURCE_DIR}/core"
    "${CMAKE_SOURCE_DIR}/interfaces"
    "${CMAKE_SOURCE_DIR}/plugins/Base_Gemo"
    "${CMAKE_SOURCE_DIR}/plugins/Base_Gemo/core"
    "${CMAKE_SOURCE_DIR}/plugins/Base_Gemo/builders"
    "${CMAKE_SOURCE_DIR}/plugins/Base_Gemo/workbench"
    "${CMAKE_SOURCE_DIR}/plugins/Base_Gemo/terrain")
target_link_libraries(GdalTerrainChecks PRIVATE Base_Gemo core Qt6::Widgets CAE::OCC CAE::VTK CAE::GDAL)
cae_vtk_autoinit(GdalTerrainChecks)
add_test(NAME GdalTerrainChecks COMMAND GdalTerrainChecks)
set_tests_properties(GdalTerrainChecks PROPERTIES ENVIRONMENT "PATH=${_smesh_test_path}")

add_executable(TerrainDisplayChecks TerrainDisplayChecks.cpp
    ../plugins/Base_Gemo/builders/DxfReader.cpp ../res.qrc)
target_include_directories(TerrainDisplayChecks PRIVATE "${CMAKE_SOURCE_DIR}/core" "${CMAKE_SOURCE_DIR}/interfaces" "${CMAKE_SOURCE_DIR}/plugins/Base_Gemo/workbench")
target_link_libraries(TerrainDisplayChecks PRIVATE Base_Gemo core Qt6::Widgets CAE::OCC CAE::VTK CAE::GDAL)
add_dependencies(TerrainDisplayChecks Base_Gemo Base_UiFrame Base_Visualization Base_ProjectManager Base_Output Base_Mesh)
cae_vtk_autoinit(TerrainDisplayChecks)
add_test(NAME TerrainDisplayChecks COMMAND TerrainDisplayChecks)
set_tests_properties(TerrainDisplayChecks PROPERTIES TIMEOUT 180 ENVIRONMENT "PATH=${_smesh_test_path}")
