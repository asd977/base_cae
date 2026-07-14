include_guard(GLOBAL)

if(NOT EXISTS "${CAE_GMSH_ROOT}/include")
    message(FATAL_ERROR "Gmsh include directory not found: ${CAE_GMSH_ROOT}/include")
endif()

find_library(CAE_GMSH_LIBRARY
    NAMES gmsh
    PATHS "${CAE_GMSH_ROOT}/lib"
    NO_DEFAULT_PATH
)

if(NOT CAE_GMSH_LIBRARY)
    message(FATAL_ERROR "Gmsh library not found under: ${CAE_GMSH_ROOT}/lib")
endif()

set(_cae_gmsh_system_libs "")
if(WIN32)
    list(APPEND _cae_gmsh_system_libs Ws2_32 Shell32 Winmm)
endif()

add_library(CAE::Gmsh INTERFACE IMPORTED GLOBAL)

set_target_properties(CAE::Gmsh PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${CAE_GMSH_ROOT}/include"
    INTERFACE_LINK_LIBRARIES "${CAE_GMSH_LIBRARY};${_cae_gmsh_system_libs}"
    INTERFACE_COMPILE_FEATURES cxx_std_11
)
