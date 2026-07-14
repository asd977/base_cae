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
