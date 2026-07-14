cmake_minimum_required(VERSION 3.16)

project(CAE_BASE LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

if(MSVC)
    add_compile_options(/utf-8 /MP)
endif()

set(CAE_RUNTIME_DIR "${CMAKE_BINARY_DIR}")
set(CAE_PLUGIN_DIR "${CMAKE_BINARY_DIR}/plugins")
set(CAE_LIB_DIR "${CMAKE_BINARY_DIR}/lib")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CAE_RUNTIME_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CAE_RUNTIME_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CAE_LIB_DIR}")

foreach(_config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${_config} "${CAE_RUNTIME_DIR}")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${_config} "${CAE_RUNTIME_DIR}")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${_config} "${CAE_LIB_DIR}")
endforeach()

set(CAE_3RDPARTY_ROOT
    "${CMAKE_SOURCE_DIR}/libs"
    CACHE PATH
    "Root folder of third-party libraries"
)

get_filename_component(_cae_default_tmsr_libs "${CMAKE_SOURCE_DIR}/../TMSR/libs" ABSOLUTE)
set(CAE_TMSR_LIBS_ROOT
    "${_cae_default_tmsr_libs}"
    CACHE PATH
    "Optional TMSR libs folder used for Qt6-built third-party packages"
)

set(CAE_QT_ROOT
    ""
    CACHE PATH
    "Qt install root, for example D:/Qt/6.5.3/msvc2019_64"
)

if(CAE_QT_ROOT AND EXISTS "${CAE_QT_ROOT}")
    list(APPEND CMAKE_PREFIX_PATH "${CAE_QT_ROOT}")
endif()

find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    PrintSupport
    Xml
    Charts
    Sql
    Svg
)

find_package(Qt6 QUIET COMPONENTS OpenGLWidgets)

set(CAE_GMSH_ROOT
    "${CAE_3RDPARTY_ROOT}/gmsh-4-15"
    CACHE PATH
    "Gmsh root"
)

set(CAE_OCC_ROOT
    "${CAE_3RDPARTY_ROOT}/OCC"
    CACHE PATH
    "OpenCASCADE root"
)

set(CAE_DOCKING_ROOT
    "${CAE_TMSR_LIBS_ROOT}/Qt6-AdvancedDock"
    CACHE PATH
    "Qt Advanced Docking root"
)

set(CAE_SARIBBON_ROOT
    "${CAE_TMSR_LIBS_ROOT}/SARibbon-qt6"
    CACHE PATH
    "SARibbon root"
)

set(CAE_VTK_ROOT
    "${CAE_TMSR_LIBS_ROOT}/vtk/vtk_release"
    CACHE PATH
    "VTK release install root"
)

set(CAE_VTK_DIR
    ""
    CACHE PATH
    "Optional path containing VTKConfig.cmake"
)

include("${CMAKE_SOURCE_DIR}/cmake/ThirdPartyGmsh.cmake")
include("${CMAKE_SOURCE_DIR}/cmake/ThirdPartyOCC.cmake")
include("${CMAKE_SOURCE_DIR}/cmake/ThirdPartyDockingWidget.cmake")
include("${CMAKE_SOURCE_DIR}/cmake/ThirdPartySARibbon.cmake")
include("${CMAKE_SOURCE_DIR}/cmake/ThirdPartyVTK.cmake")

add_subdirectory(core)
add_subdirectory(plugins)
add_subdirectory(launcher)
