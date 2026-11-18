set(CAE_BASE_PLUGIN_DIRS
    Base_UiFrame
    Base_Output
    Base_ProjectManager
    Base_Visualization
    Base_Gemo
    Base_Mesh
    Base_MaterialManager
    Base_PostProcessing
    Base_Solver
    Base_GlobalParameter
    Base_SurrogateModel
    Base_PhysicalField
    Base_Display
)

function(cae_collect_target_files out_headers out_sources out_forms out_resources source_dir)
    file(GLOB_RECURSE _headers CONFIGURE_DEPENDS
        "${source_dir}/*.h"
        "${source_dir}/*.hpp"
    )

    file(GLOB_RECURSE _sources CONFIGURE_DEPENDS
        "${source_dir}/*.cpp"
        "${source_dir}/*.cxx"
        "${source_dir}/*.cc"
    )

    file(GLOB_RECURSE _forms CONFIGURE_DEPENDS
        "${source_dir}/*.ui"
    )

    file(GLOB_RECURSE _resources CONFIGURE_DEPENDS
        "${source_dir}/*.qrc"
    )

    foreach(_list_name _headers _sources _forms _resources)
        list(FILTER ${_list_name} EXCLUDE REGEX ".*/moc_.*")
        list(FILTER ${_list_name} EXCLUDE REGEX ".*/mocs_.*")
        list(FILTER ${_list_name} EXCLUDE REGEX ".*/qrc_.*\\.cpp$")
        list(FILTER ${_list_name} EXCLUDE REGEX ".*/ui_.*\\.h$")
        list(FILTER ${_list_name} EXCLUDE REGEX ".*/moc_predefs\\.h$")
        list(FILTER ${_list_name} EXCLUDE REGEX ".*/[^/\\\\]+-[0-9]+\\.(h|hpp|cpp|cxx|cc|ui|qrc)$")
        list(FILTER ${_list_name} EXCLUDE REGEX ".*\\.bak$")
        list(FILTER ${_list_name} EXCLUDE REGEX ".*\\.orig$")
        list(FILTER ${_list_name} EXCLUDE REGEX ".*\\.tmp$")
    endforeach()

    set(${out_headers} ${_headers} PARENT_SCOPE)
    set(${out_sources} ${_sources} PARENT_SCOPE)
    set(${out_forms} ${_forms} PARENT_SCOPE)
    set(${out_resources} ${_resources} PARENT_SCOPE)
endfunction()

function(cae_add_base_plugin plugin_dir)
    set(_dir "${CMAKE_CURRENT_SOURCE_DIR}/${plugin_dir}")

    if(NOT EXISTS "${_dir}")
        message(STATUS "Skipping ${plugin_dir}: directory not found")
        return()
    endif()

    cae_collect_target_files(_headers _sources _forms _resources "${_dir}")

    # The SMESH/NETGEN implementation is built exclusively into the standalone
    # CAE_SMESHBridge DLL. Base_Mesh accesses it through SMESHBridgeApi.h and
    # QLibrary; compiling SMESHBridge.cpp into Base_Mesh as well would require
    # the native SMESH SDK in the UI plugin and duplicate the bridge exports.
    if(plugin_dir STREQUAL "Base_Mesh")
        list(FILTER _sources EXCLUDE REGEX "/tools/SMESHBridge\\.cpp$")
    endif()

    if(plugin_dir STREQUAL "Base_ProjectManager")
        list(FILTER _headers EXCLUDE REGEX ".*/ProjectManagerWidget\\.h$")
        list(FILTER _sources EXCLUDE REGEX ".*/ProjectManagerWidget\\.cpp$")
    endif()

    if(plugin_dir STREQUAL "Base_PhysicalField")
        list(FILTER _headers EXCLUDE REGEX ".*/DisplacementConstraintSelectionManager\\.h$")
        list(FILTER _sources EXCLUDE REGEX ".*/DisplacementConstraintSelectionManager\\.cpp$")
        list(FILTER _headers EXCLUDE REGEX ".*/SelectionStore\\.h$")
        list(FILTER _sources EXCLUDE REGEX ".*/SelectionStore\\.cpp$")
    endif()

    if(NOT _headers AND NOT _sources AND NOT _forms AND NOT _resources)
        message(STATUS "Skipping ${plugin_dir}: no source files found")
        return()
    endif()

    set(_all_sources
        ${_headers}
        ${_sources}
        ${_forms}
        ${_resources}
    )

    if(EXISTS "${CMAKE_SOURCE_DIR}/res.qrc")
        list(APPEND _all_sources "${CMAKE_SOURCE_DIR}/res.qrc")
    endif()

    add_library(${plugin_dir} SHARED
        ${_all_sources}
    )

    if(plugin_dir STREQUAL "Base_Gemo")
        target_compile_definitions(${plugin_dir} PRIVATE GEOMETRY_LIBRARY)
    endif()

    set(_include_dirs
        ${_dir}
        ${CMAKE_SOURCE_DIR}/core
        ${CMAKE_SOURCE_DIR}/interfaces
    )

    foreach(_file IN LISTS _headers _sources _forms)
        get_filename_component(_file_dir "${_file}" DIRECTORY)
        list(APPEND _include_dirs "${_file_dir}")
    endforeach()

    list(REMOVE_DUPLICATES _include_dirs)

    target_include_directories(${plugin_dir}
        PRIVATE
            ${_include_dirs}
    )

    set(_qt_libs
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::PrintSupport
        Qt6::Xml
        Qt6::Charts
        Qt6::Sql
        Qt6::Svg
    )

    if(TARGET Qt6::OpenGLWidgets)
        list(APPEND _qt_libs Qt6::OpenGLWidgets)
    endif()

    target_link_libraries(${plugin_dir}
        PRIVATE
            core
            ${_qt_libs}
            # NETGEN is consumed inside the standalone SMESH bridge, not
            # through a direct CAE::Netgen target on every Qt plugin.
            CAE::OCC
            CAE::DockingWidget
            CAE::SARibbon
            CAE::VTK
    )

    if(_forms)
        set(_uic_search_paths "")
        foreach(_form IN LISTS _forms)
            get_filename_component(_form_dir "${_form}" DIRECTORY)
            list(APPEND _uic_search_paths "${_form_dir}")
        endforeach()
        list(REMOVE_DUPLICATES _uic_search_paths)
        set_property(TARGET ${plugin_dir} PROPERTY AUTOUIC_SEARCH_PATHS ${_uic_search_paths})
    endif()

    if(COMMAND cae_vtk_autoinit)
        cae_vtk_autoinit(${plugin_dir})
    endif()

    if(COMMAND cae_copy_netgen_runtime)
        cae_copy_netgen_runtime(${plugin_dir})
    endif()

    if(COMMAND cae_copy_occ_runtime)
        cae_copy_occ_runtime(${plugin_dir})
    endif()

    if(COMMAND cae_copy_docking_runtime)
        cae_copy_docking_runtime(${plugin_dir})
    endif()

    if(COMMAND cae_copy_saribbon_runtime)
        cae_copy_saribbon_runtime(${plugin_dir})
    endif()

    set_target_properties(${plugin_dir} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CAE_PLUGIN_DIR}"
        LIBRARY_OUTPUT_DIRECTORY "${CAE_PLUGIN_DIR}"
        ARCHIVE_OUTPUT_DIRECTORY "${CAE_LIB_DIR}"

        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CAE_PLUGIN_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CAE_PLUGIN_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CAE_PLUGIN_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CAE_PLUGIN_DIR}"

        LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CAE_PLUGIN_DIR}"
        LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CAE_PLUGIN_DIR}"
        LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CAE_PLUGIN_DIR}"
        LIBRARY_OUTPUT_DIRECTORY_MINSIZEREL "${CAE_PLUGIN_DIR}"

        ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${CAE_LIB_DIR}"
        ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${CAE_LIB_DIR}"
        ARCHIVE_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CAE_LIB_DIR}"
        ARCHIVE_OUTPUT_DIRECTORY_MINSIZEREL "${CAE_LIB_DIR}"
    )
endfunction()

foreach(_plugin IN LISTS CAE_BASE_PLUGIN_DIRS)
    cae_add_base_plugin(${_plugin})
endforeach()

# The standalone SMESH backend lives in a separately loaded DLL.  Register it
# after Base_Mesh exists so callers (including meshing check targets in the
# parent CMake project) may safely depend on CAE_SMESHBridge.
# Do not create a dummy target: the real SDK/OCCT compatibility checks in the
# backend module must still run.
if(TARGET Base_Mesh AND NOT TARGET CAE_SMESHBridge)
    include("${CMAKE_CURRENT_SOURCE_DIR}/Base_Mesh/cmake/SMESHBackend.cmake")
    cae_enable_smesh_backend(Base_Mesh)
endif()

