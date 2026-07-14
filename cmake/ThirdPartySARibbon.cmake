include_guard(GLOBAL)

if(NOT EXISTS "${CAE_SARIBBON_ROOT}/release/include")
    message(FATAL_ERROR "SARibbon include directory not found: ${CAE_SARIBBON_ROOT}/release/include")
endif()

find_library(CAE_SARIBBON_DEBUG_LIBRARY
    NAMES SARibbonBard
    PATHS "${CAE_SARIBBON_ROOT}/debug/lib"
    NO_DEFAULT_PATH
)

find_library(CAE_SARIBBON_RELEASE_LIBRARY
    NAMES SARibbonBar
    PATHS "${CAE_SARIBBON_ROOT}/release/lib"
    NO_DEFAULT_PATH
)

if(NOT CAE_SARIBBON_DEBUG_LIBRARY)
    message(FATAL_ERROR "SARibbon debug library not found under: ${CAE_SARIBBON_ROOT}/debug/lib")
endif()

if(NOT CAE_SARIBBON_RELEASE_LIBRARY)
    message(FATAL_ERROR "SARibbon release library not found under: ${CAE_SARIBBON_ROOT}/release/lib")
endif()

add_library(CAE::SARibbon INTERFACE IMPORTED GLOBAL)

set_target_properties(CAE::SARibbon PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${CAE_SARIBBON_ROOT}/release/include"
    INTERFACE_LINK_LIBRARIES "$<$<CONFIG:Debug>:${CAE_SARIBBON_DEBUG_LIBRARY}>;$<$<NOT:$<CONFIG:Debug>>:${CAE_SARIBBON_RELEASE_LIBRARY}>"
    INTERFACE_COMPILE_DEFINITIONS "$<$<CONFIG:Debug>:_DEBUG>"
    INTERFACE_COMPILE_FEATURES cxx_std_11
)
