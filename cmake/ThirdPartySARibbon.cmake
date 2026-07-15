include_guard(GLOBAL)

if(NOT EXISTS "${CAE_SARIBBON_ROOT}/release/include")
    message(FATAL_ERROR "SARibbon include directory not found: ${CAE_SARIBBON_ROOT}/release/include")
endif()

unset(CAE_SARIBBON_DEBUG_LIBRARY CACHE)
unset(CAE_SARIBBON_RELEASE_LIBRARY CACHE)
unset(CAE_SARIBBON_DEBUG_RUNTIME_DLL CACHE)
unset(CAE_SARIBBON_RELEASE_RUNTIME_DLL CACHE)

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

find_file(CAE_SARIBBON_DEBUG_RUNTIME_DLL
    NAMES SARibbonBard.dll
    PATHS "${CAE_SARIBBON_ROOT}/debug/bin" "${CAE_SARIBBON_ROOT}/debug/lib"
    NO_DEFAULT_PATH
)

find_file(CAE_SARIBBON_RELEASE_RUNTIME_DLL
    NAMES SARibbonBar.dll
    PATHS "${CAE_SARIBBON_ROOT}/release/bin" "${CAE_SARIBBON_ROOT}/release/lib"
    NO_DEFAULT_PATH
)

if(NOT CAE_SARIBBON_DEBUG_LIBRARY)
    message(FATAL_ERROR "SARibbon debug library not found under: ${CAE_SARIBBON_ROOT}/debug/lib")
endif()

if(NOT CAE_SARIBBON_RELEASE_LIBRARY)
    message(FATAL_ERROR "SARibbon release library not found under: ${CAE_SARIBBON_ROOT}/release/lib")
endif()

if(WIN32 AND NOT CAE_SARIBBON_RELEASE_RUNTIME_DLL)
    message(FATAL_ERROR "SARibbon release runtime DLL not found under: ${CAE_SARIBBON_ROOT}/release")
endif()

add_library(CAE::SARibbon INTERFACE IMPORTED GLOBAL)

set_target_properties(CAE::SARibbon PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${CAE_SARIBBON_ROOT}/release/include"
    INTERFACE_LINK_LIBRARIES "$<$<CONFIG:Debug>:${CAE_SARIBBON_DEBUG_LIBRARY}>;$<$<NOT:$<CONFIG:Debug>>:${CAE_SARIBBON_RELEASE_LIBRARY}>"
    INTERFACE_COMPILE_DEFINITIONS "$<$<CONFIG:Debug>:_DEBUG>"
    INTERFACE_COMPILE_FEATURES cxx_std_11
)

function(cae_copy_saribbon_runtime target_name)
    if(WIN32)
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<$<CONFIG:Debug>:${CAE_SARIBBON_DEBUG_RUNTIME_DLL}>$<$<NOT:$<CONFIG:Debug>>:${CAE_SARIBBON_RELEASE_RUNTIME_DLL}>"
                "${CAE_RUNTIME_DIR}"
            VERBATIM
        )
    endif()
endfunction()
