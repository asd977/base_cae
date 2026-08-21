add_executable(CAE_BASE WIN32
    launcher.cpp
    ${CMAKE_SOURCE_DIR}/res.qrc
)

# Windows Explorer/taskbar executable icon. OUTPUT_NAME only changes the file
# name; the .ico still has to be compiled into the internal CMake target.
if(WIN32)
    set(CAE_APP_ICON "${CMAKE_SOURCE_DIR}/resources/icon.ico")
    if(EXISTS "${CAE_APP_ICON}")
        # Keep forward slashes in the generated .rc file. A native Windows path
        # contains sequences such as "\r" in "\resources", which RC parses as
        # an escape character and turns into a carriage return.
        file(TO_CMAKE_PATH "${CAE_APP_ICON}" CAE_APP_ICON_RC)
        configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/app.rc.in"
            "${CMAKE_CURRENT_BINARY_DIR}/app.rc"
            @ONLY
        )
        target_sources(CAE_BASE PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/app.rc")
    else()
        message(WARNING "Application icon was not found: ${CAE_APP_ICON}")
    endif()
endif()

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
    OUTPUT_NAME "POWERCHINA-GEOCAE"
    RUNTIME_OUTPUT_DIRECTORY "${CAE_RUNTIME_DIR}"
)

set(CAE_SOLVER_EXECUTABLE
    "${CMAKE_SOURCE_DIR}/solver/PCCAE_CPU/PCCAE_CPU.exe"
    CACHE FILEPATH
    "Prebuilt solver executable deployed with CAE_BASE"
)

set(CAE_SLOPE_ENGINE_EXECUTABLE
    "${CMAKE_SOURCE_DIR}/solver/SlopeEngine/SlopeEngine.exe"
    CACHE FILEPATH
    "Prebuilt SlopeEngine executable used by the slope standard-method check"
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

    if(EXISTS "${CAE_SLOPE_ENGINE_EXECUTABLE}")
        add_custom_target(CAE_DEPLOY_SLOPE_ENGINE ALL
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "${CAE_RUNTIME_DIR}/solver/SlopeEngine"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CAE_SLOPE_ENGINE_EXECUTABLE}"
                "${CAE_RUNTIME_DIR}/solver/SlopeEngine/SlopeEngine.exe"
            DEPENDS "${CAE_SLOPE_ENGINE_EXECUTABLE}"
            COMMENT "Deploying slope standard-method engine"
            VERBATIM
        )
        add_dependencies(CAE_BASE CAE_DEPLOY_SLOPE_ENGINE)
    else()
        message(WARNING
            "SlopeEngine executable was not found at ${CAE_SLOPE_ENGINE_EXECUTABLE}; "
            "set CAE_SLOPE_ENGINE_EXECUTABLE to the delivered SlopeEngine.exe")
    endif()
endif()
