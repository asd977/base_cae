add_executable(CAE_BASE WIN32
    launcher.cpp
    ${CMAKE_SOURCE_DIR}/res.qrc
)

target_include_directories(CAE_BASE
    PRIVATE
        ${CMAKE_SOURCE_DIR}/core
        ${CMAKE_SOURCE_DIR}/interfaces
)

target_link_libraries(CAE_BASE
    PRIVATE
        core
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
)

set_target_properties(CAE_BASE PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CAE_RUNTIME_DIR}"
)

set(CAE_SOLVER_EXECUTABLE
    "${CMAKE_SOURCE_DIR}/solver/PCCAE_CPU/PCCAE_CPU.exe"
    CACHE FILEPATH
    "Prebuilt solver executable deployed with CAE_BASE"
)

if(WIN32)
    set(_cae_runtime_plugin_targets
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
    )
    set(_cae_runtime_binaries "$<TARGET_FILE:CAE_BASE>")
    foreach(_cae_runtime_plugin_target IN LISTS _cae_runtime_plugin_targets)
        if(TARGET ${_cae_runtime_plugin_target})
            add_dependencies(CAE_BASE ${_cae_runtime_plugin_target})
            list(APPEND _cae_runtime_binaries "$<TARGET_FILE:${_cae_runtime_plugin_target}>")
        endif()
    endforeach()

    get_target_property(_cae_qmake_executable Qt6::qmake IMPORTED_LOCATION)
    if(_cae_qmake_executable)
        get_filename_component(_cae_qt_bin_dir "${_cae_qmake_executable}" DIRECTORY)
        find_program(_cae_windeployqt
            NAMES windeployqt
            HINTS "${_cae_qt_bin_dir}"
            NO_DEFAULT_PATH
        )
    endif()

    if(_cae_windeployqt)
        add_custom_command(TARGET CAE_BASE POST_BUILD
            COMMAND "${_cae_windeployqt}"
                --no-translations
                --no-compiler-runtime
                --skip-plugin-types generic,networkinformation,tls
                --dir "${CAE_RUNTIME_DIR}"
                ${_cae_runtime_binaries}
            COMMENT "Deploying Qt runtime files"
            COMMAND_EXPAND_LISTS
            VERBATIM
        )
    else()
        message(WARNING "windeployqt was not found; the release folder may require a Qt PATH")
    endif()

    execute_process(
        COMMAND "${_cae_qmake_executable}" -query QT_INSTALL_TRANSLATIONS
        OUTPUT_VARIABLE _cae_qt_translations_dir
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    set(_cae_qt_chinese_translation
        "${_cae_qt_translations_dir}/qtbase_zh_CN.qm")
    if(EXISTS "${_cae_qt_chinese_translation}")
        add_custom_command(TARGET CAE_BASE POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "${CAE_RUNTIME_DIR}/translations"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_cae_qt_chinese_translation}"
                "${CAE_RUNTIME_DIR}/translations/qtbase_zh_CN.qm"
            COMMENT "Deploying Chinese Qt translations"
            VERBATIM
        )
    endif()

    file(GLOB _cae_vtk_runtime_dlls "${CAE_VTK_ROOT}/bin/*.dll")
    foreach(_cae_vtk_runtime_dll IN LISTS _cae_vtk_runtime_dlls)
        add_custom_command(TARGET CAE_BASE POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_cae_vtk_runtime_dll}"
                "${CAE_RUNTIME_DIR}"
            VERBATIM
        )
    endforeach()

    if(EXISTS "${CAE_SOLVER_EXECUTABLE}")
        add_custom_target(CAE_DEPLOY_SOLVER ALL
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "${CAE_RUNTIME_DIR}/solver/PCCAE_CPU"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CAE_SOLVER_EXECUTABLE}"
                "${CAE_RUNTIME_DIR}/solver/PCCAE_CPU/PCCAE_CPU.exe"
            DEPENDS "${CAE_SOLVER_EXECUTABLE}"
            COMMENT "Deploying solver executable"
            VERBATIM
        )
        add_dependencies(CAE_BASE CAE_DEPLOY_SOLVER)
    else()
        message(WARNING
            "Solver executable was not found at ${CAE_SOLVER_EXECUTABLE}; "
            "set CAE_SOLVER_EXECUTABLE to a valid prebuilt solver")
    endif()
endif()
