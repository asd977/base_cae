file(GLOB_RECURSE CORE_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
)

file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cxx"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)

foreach(_list_name CORE_HEADERS CORE_SOURCES)
    list(FILTER ${_list_name} EXCLUDE REGEX ".*/moc_.*")
    list(FILTER ${_list_name} EXCLUDE REGEX ".*/mocs_.*")
    list(FILTER ${_list_name} EXCLUDE REGEX ".*/qrc_.*\\.cpp$")
    list(FILTER ${_list_name} EXCLUDE REGEX ".*/ui_.*\\.h$")
    list(FILTER ${_list_name} EXCLUDE REGEX ".*/moc_predefs\\.h$")
    list(FILTER ${_list_name} EXCLUDE REGEX ".*/[^/\\\\]+-[0-9]+\\.(h|hpp|cpp|cxx|cc)$")
endforeach()

add_library(core SHARED
    ${CORE_HEADERS}
    ${CORE_SOURCES}
)

target_compile_definitions(core
    PRIVATE
        CORE_LIB
)

target_include_directories(core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/interfaces
)

target_link_libraries(core
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        CAE::VTK
)

if(TARGET Qt6::OpenGLWidgets)
    target_link_libraries(core PUBLIC Qt6::OpenGLWidgets)
endif()

if(COMMAND cae_vtk_autoinit)
    cae_vtk_autoinit(core)
endif()

set_target_properties(core PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CAE_RUNTIME_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${CAE_RUNTIME_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY "${CAE_LIB_DIR}"
)
