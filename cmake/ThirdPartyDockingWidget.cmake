include_guard(GLOBAL)

if(NOT EXISTS "${CAE_DOCKING_ROOT}/include")
    message(FATAL_ERROR "DockingWidget include directory not found: ${CAE_DOCKING_ROOT}/include")
endif()

find_library(CAE_DOCKING_DEBUG_LIBRARY
    NAMES qtadvanceddockingd
    PATHS "${CAE_DOCKING_ROOT}/debug"
    NO_DEFAULT_PATH
)

find_library(CAE_DOCKING_RELEASE_LIBRARY
    NAMES qtadvanceddocking
    PATHS "${CAE_DOCKING_ROOT}/release"
    NO_DEFAULT_PATH
)

if(NOT CAE_DOCKING_DEBUG_LIBRARY)
    message(FATAL_ERROR "DockingWidget debug library not found under: ${CAE_DOCKING_ROOT}/debug")
endif()

if(NOT CAE_DOCKING_RELEASE_LIBRARY)
    message(FATAL_ERROR "DockingWidget release library not found under: ${CAE_DOCKING_ROOT}/release")
endif()

add_library(CAE::DockingWidget INTERFACE IMPORTED GLOBAL)

set_target_properties(CAE::DockingWidget PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${CAE_DOCKING_ROOT}/include"
    INTERFACE_LINK_LIBRARIES "$<$<CONFIG:Debug>:${CAE_DOCKING_DEBUG_LIBRARY}>;$<$<NOT:$<CONFIG:Debug>>:${CAE_DOCKING_RELEASE_LIBRARY}>"
    INTERFACE_COMPILE_DEFINITIONS "$<$<CONFIG:Debug>:_DEBUG>"
    INTERFACE_COMPILE_FEATURES cxx_std_11
)
