add_executable(SmeshMeshingChecks
    SmeshMeshingChecks.cpp
    ../plugins/Base_Mesh/SmeshMeshGenerator.cpp)
target_include_directories(SmeshMeshingChecks PRIVATE "${CMAKE_SOURCE_DIR}/interfaces")
target_link_libraries(SmeshMeshingChecks PRIVATE Qt6::Core Qt6::Gui CAE::OCC CAE::VTK)
add_dependencies(SmeshMeshingChecks CAE_SMESHBridge)
cae_vtk_autoinit(SmeshMeshingChecks)
add_test(NAME SmeshMeshingChecks COMMAND SmeshMeshingChecks)
string(REPLACE ";" "\\;" _smesh_test_path "${CAE_QT_ROOT}/bin;$ENV{PATH}")
set_tests_properties(SmeshMeshingChecks PROPERTIES TIMEOUT 180
    ENVIRONMENT "PATH=${_smesh_test_path}")

add_executable(SmeshWorkflowChecks SmeshWorkflowChecks.cpp ../plugins/Base_Mesh/SmeshMeshGenerator.cpp)
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

add_executable(UiGeometryPostChecks UiGeometryPostChecks.cpp ../res.qrc
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

add_executable(SlopeExcavationChecks SlopeExcavationChecks.cpp)
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
