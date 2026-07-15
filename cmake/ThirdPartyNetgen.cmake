if(NOT EXISTS "${CAE_NETGEN_ROOT}/include" AND NOT EXISTS "${CAE_NETGEN_ROOT}/nglib")
    message(FATAL_ERROR "Netgen include directory not found. Set CAE_NETGEN_ROOT to a Netgen install/source root: ${CAE_NETGEN_ROOT}")
endif()

find_path(CAE_NETGEN_INCLUDE_DIR
    NAMES nglib.h
    PATHS
        "${CAE_NETGEN_ROOT}/include"
        "${CAE_NETGEN_ROOT}/include/netgen"
        "${CAE_NETGEN_ROOT}/nglib"
    NO_DEFAULT_PATH
)

find_path(CAE_NETGEN_OCC_INCLUDE_DIR
    NAMES nglib_occ.h
    PATHS
        "${CAE_NETGEN_ROOT}/include"
        "${CAE_NETGEN_ROOT}/include/netgen"
        "${CAE_NETGEN_ROOT}/nglib"
    NO_DEFAULT_PATH
)

find_library(CAE_NETGEN_LIBRARY
    NAMES nglib netgen
    PATHS
        "${CAE_NETGEN_ROOT}/lib"
        "${CAE_NETGEN_ROOT}/bin"
    NO_DEFAULT_PATH
)

find_library(CAE_NETGEN_CORE_LIBRARY
    NAMES ngcore
    PATHS
        "${CAE_NETGEN_ROOT}/lib"
        "${CAE_NETGEN_ROOT}/bin"
    NO_DEFAULT_PATH
)

find_file(CAE_NETGEN_RUNTIME_DLL
    NAMES nglib.dll
    PATHS "${CAE_NETGEN_ROOT}/bin"
    NO_DEFAULT_PATH
)

find_file(CAE_NETGEN_CORE_RUNTIME_DLL
    NAMES ngcore.dll
    PATHS "${CAE_NETGEN_ROOT}/bin"
    NO_DEFAULT_PATH
)

if(NOT CAE_NETGEN_INCLUDE_DIR OR NOT CAE_NETGEN_OCC_INCLUDE_DIR)
    message(FATAL_ERROR "Netgen nglib headers not found under: ${CAE_NETGEN_ROOT}")
endif()

if(NOT CAE_NETGEN_LIBRARY)
    message(FATAL_ERROR "Netgen nglib library not found under: ${CAE_NETGEN_ROOT}/lib or ${CAE_NETGEN_ROOT}/bin")
endif()

if(NOT CAE_NETGEN_CORE_LIBRARY)
    message(FATAL_ERROR "Netgen ngcore library not found under: ${CAE_NETGEN_ROOT}/lib or ${CAE_NETGEN_ROOT}/bin")
endif()

if(WIN32 AND (NOT CAE_NETGEN_RUNTIME_DLL OR NOT CAE_NETGEN_CORE_RUNTIME_DLL))
    message(FATAL_ERROR "Netgen runtime DLLs nglib.dll/ngcore.dll not found under: ${CAE_NETGEN_ROOT}/bin")
endif()

set(_cae_netgen_system_libs "")
if(WIN32)
    list(APPEND _cae_netgen_system_libs Ws2_32)
endif()

add_library(CAE::Netgen INTERFACE IMPORTED GLOBAL)

set_target_properties(CAE::Netgen PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${CAE_NETGEN_INCLUDE_DIR};${CAE_NETGEN_OCC_INCLUDE_DIR}"
    INTERFACE_LINK_LIBRARIES "${CAE_NETGEN_LIBRARY};${CAE_NETGEN_CORE_LIBRARY};${_cae_netgen_system_libs}"
)

function(cae_copy_netgen_runtime target_name)
    if(WIN32)
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CAE_NETGEN_RUNTIME_DLL}"
                "${CAE_NETGEN_CORE_RUNTIME_DLL}"
                "${CAE_RUNTIME_DIR}"
            COMMAND ${CMAKE_COMMAND} -E rm -f
                "$<TARGET_FILE_DIR:${target_name}>/nglib.dll"
                "$<TARGET_FILE_DIR:${target_name}>/ngcore.dll"
            VERBATIM
        )
    endif()
endfunction()
