cmake_minimum_required(VERSION 3.20)

project(CAE_BASE LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

if(MSVC)
    add_compile_options(/utf-8 /MP)
endif()
include("${CMAKE_SOURCE_DIR}/cmake/MsvcNinjaDependencies.cmake")

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

set(CAE_OCC_ROOT
    "${CAE_3RDPARTY_ROOT}/OCC-7.9.3"
    CACHE PATH
    "OpenCASCADE root"
)
if(CAE_OCC_ROOT STREQUAL "${CAE_3RDPARTY_ROOT}/OCC")
    set(CAE_OCC_ROOT "${CAE_3RDPARTY_ROOT}/OCC-7.9.3" CACHE PATH "OpenCASCADE root" FORCE)
endif()

set(CAE_DOCKING_ROOT
    "${CAE_3RDPARTY_ROOT}/Qt6-AdvancedDock"
)

set(CAE_SARIBBON_ROOT
    "${CAE_3RDPARTY_ROOT}/SARibbon-qt6"
)

set(CAE_VTK_ROOT
    "${CAE_3RDPARTY_ROOT}/vtk/vtk_release"
)

set(CAE_GDAL_ROOT
    "${CAE_3RDPARTY_ROOT}/gdal"
)

set(CAE_VTK_DIR
    ""
    CACHE PATH
    "Optional path containing VTKConfig.cmake"
)

include("${CMAKE_SOURCE_DIR}/cmake/ThirdPartyOCC.cmake")
include("${CMAKE_SOURCE_DIR}/cmake/ThirdPartyDockingWidget.cmake")
include("${CMAKE_SOURCE_DIR}/cmake/ThirdPartySARibbon.cmake")
include("${CMAKE_SOURCE_DIR}/cmake/ThirdPartyVTK.cmake")
include("${CMAKE_SOURCE_DIR}/cmake/ThirdPartyGDAL.cmake")

add_subdirectory(core)
add_subdirectory(plugins)
add_subdirectory(launcher)

include("${CMAKE_SOURCE_DIR}/cmake/GMDSBackend.cmake")
add_dependencies(Base_Gemo CAE_GMDSWorker)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
