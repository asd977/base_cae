include_guard(GLOBAL)

if(NOT EXISTS "${CAE_GDAL_ROOT}/include")
    message(FATAL_ERROR "GDAL include directory not found: ${CAE_GDAL_ROOT}/include")
endif()

find_library(CAE_GDAL_LIBRARY
    NAMES gdal
    PATHS "${CAE_GDAL_ROOT}/lib"
    NO_DEFAULT_PATH
)

if(NOT CAE_GDAL_LIBRARY)
    message(FATAL_ERROR "GDAL library not found under: ${CAE_GDAL_ROOT}/lib")
endif()

add_library(CAE::GDAL INTERFACE IMPORTED GLOBAL)

set_target_properties(CAE::GDAL PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${CAE_GDAL_ROOT}/include"
    INTERFACE_LINK_LIBRARIES "${CAE_GDAL_LIBRARY}"
)

function(cae_copy_gdal_runtime target_name)
    if(WIN32)
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND}
                "-DCAE_GDAL_ROOT=${CAE_GDAL_ROOT}"
                "-DCAE_RUNTIME_DIR=${CAE_RUNTIME_DIR}"
                -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/DeployGdalRuntime.cmake"
            VERBATIM
        )
    endif()
endfunction()
