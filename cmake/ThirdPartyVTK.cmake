include_guard(GLOBAL)

if(CAE_VTK_DIR AND EXISTS "${CAE_VTK_DIR}/VTKConfig.cmake")
    list(APPEND CMAKE_PREFIX_PATH "${CAE_VTK_DIR}")
elseif(CAE_VTK_DIR AND EXISTS "${CAE_VTK_DIR}/vtk-config.cmake")
    list(APPEND CMAKE_PREFIX_PATH "${CAE_VTK_DIR}")
elseif(EXISTS "${CAE_VTK_ROOT}/lib/cmake")
    file(GLOB _vtk_config_dirs LIST_DIRECTORIES true "${CAE_VTK_ROOT}/lib/cmake/vtk-*")
    if(_vtk_config_dirs)
        list(SORT _vtk_config_dirs)
        list(REVERSE _vtk_config_dirs)
        list(GET _vtk_config_dirs 0 _vtk_first_config_dir)
        list(APPEND CMAKE_PREFIX_PATH "${_vtk_first_config_dir}")
    endif()
endif()

if(DEFINED _vtk_first_config_dir)
    set(_cae_vtk_config_file "${_vtk_first_config_dir}/vtk-config.cmake")
elseif(CAE_VTK_DIR AND EXISTS "${CAE_VTK_DIR}/vtk-config.cmake")
    set(_cae_vtk_config_file "${CAE_VTK_DIR}/vtk-config.cmake")
elseif(CAE_VTK_DIR AND EXISTS "${CAE_VTK_DIR}/VTKConfig.cmake")
    set(_cae_vtk_config_file "${CAE_VTK_DIR}/VTKConfig.cmake")
endif()

if(_cae_vtk_config_file AND EXISTS "${_cae_vtk_config_file}")
    get_filename_component(VTK_DIR "${_cae_vtk_config_file}" DIRECTORY)
    file(STRINGS "${_cae_vtk_config_file}" _cae_vtk_qt_version_line
        REGEX "if \\([0-9]+\\) # VTK_QT_VERSION"
    )
    if(_cae_vtk_qt_version_line MATCHES "if \\(5\\)")
        message(FATAL_ERROR
            "The selected VTK package was built with Qt5: ${_cae_vtk_config_file}\n"
            "This project is configured for Qt6. Set CAE_VTK_ROOT to a Qt6-built VTK folder, "
            "expected under ${CAE_3RDPARTY_ROOT}/vtk/vtk_release."
        )
    endif()
endif()

find_package(VTK CONFIG REQUIRED)

if(NOT VTK_LIBRARIES)
    message(FATAL_ERROR
        "VTK was found, but VTK_LIBRARIES is empty.\n"
        "Please check VTKConfig.cmake under: ${CAE_VTK_ROOT}/lib/cmake/vtk-*\n"
        "Or set CAE_VTK_DIR to the exact folder containing VTKConfig.cmake."
    )
endif()

add_library(CAE::VTK INTERFACE IMPORTED GLOBAL)

set_target_properties(CAE::VTK PROPERTIES
    INTERFACE_LINK_LIBRARIES "${VTK_LIBRARIES}"
    INTERFACE_COMPILE_FEATURES cxx_std_11
)

function(cae_vtk_autoinit target_name)
    if(NOT TARGET ${target_name})
        message(FATAL_ERROR "cae_vtk_autoinit: target not found: ${target_name}")
    endif()

    if(COMMAND vtk_module_autoinit)
        vtk_module_autoinit(
            TARGETS ${target_name}
            MODULES ${VTK_LIBRARIES}
        )
    endif()
endfunction()
