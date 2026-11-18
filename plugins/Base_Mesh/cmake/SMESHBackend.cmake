# Standalone SALOME SMESH bridge.
#
# The bridge is a DLL loaded by Base_Mesh at runtime. It is deliberately built
# with the same OCCT and VTK packages as the host application.

include_guard(GLOBAL)

set(CAE_SMESH_ROOT "${CAE_3RDPARTY_ROOT}/SMESH-occ-7.9.3" CACHE PATH
    "Installed standalone SMESH prefix")
if(CAE_SMESH_ROOT STREQUAL "${CAE_3RDPARTY_ROOT}/SMESH"
   AND EXISTS "${CAE_3RDPARTY_ROOT}/SMESH-occ-7.9.3/lib/StdMeshers.lib")
    set(CAE_SMESH_ROOT "${CAE_3RDPARTY_ROOT}/SMESH-occ-7.9.3" CACHE PATH "Installed standalone SMESH prefix" FORCE)
endif()
set(CAE_SMESH_BOOST_ROOT
    "${CMAKE_SOURCE_DIR}/libs/boost_1_84_0"
    CACHE PATH "Boost runtime used to build standalone SMESH")
set(CAE_SMESH_PTHREAD_ROOT "${CAE_3RDPARTY_ROOT}/pthreads-win32" CACHE PATH
    "pthreads-win32 prefix used to build standalone SMESH")

function(cae_enable_smesh_backend mesh_target)
    if(NOT TARGET ${mesh_target})
        message(FATAL_ERROR "cae_enable_smesh_backend: target '${mesh_target}' does not exist")
    endif()

    set(_smesh_inc "${CAE_SMESH_ROOT}/include/smesh")
    set(_smesh_lib "${CAE_SMESH_ROOT}/lib")
    set(_smesh_bin "${CAE_SMESH_ROOT}/bin")
    if(NOT EXISTS "${CAE_SMESH_ROOT}/occ-link-info.json")
        message(FATAL_ERROR "SMESH must provide its validated OCCT build metadata: ${CAE_SMESH_ROOT}/occ-link-info.json")
    endif()
    file(READ "${CAE_SMESH_ROOT}/occ-link-info.json" _smesh_occ_info)
    string(JSON _smesh_occ_version GET "${_smesh_occ_info}" occ_version)
    string(JSON _smesh_occ_hash GET "${_smesh_occ_info}" occ_tkernel_sha256)
    file(SHA256 "${CAE_OCC_ROOT}/bin/TKernel.dll" _selected_occ_hash)
    if(NOT _smesh_occ_version STREQUAL CAE_OCC_VERSION OR NOT _smesh_occ_hash STREQUAL _selected_occ_hash)
        message(FATAL_ERROR "SMESH and CAE must share the same validated OCCT DLL package; rebuild SMESH for ${CAE_OCC_ROOT}")
    endif()
    if(NOT EXISTS "${_smesh_inc}/SMESH/SMESH_Gen.hxx" OR
       NOT EXISTS "${_smesh_lib}/StdMeshers.lib")
        message(FATAL_ERROR
            "Standalone SMESH was not found at ${CAE_SMESH_ROOT}. "
            "Install the standalone SMESH package and set CAE_SMESH_ROOT.")
    endif()

    set(_smesh_boost_root "${CAE_SMESH_BOOST_ROOT}")
    if(NOT EXISTS "${_smesh_boost_root}/boost/shared_ptr.hpp")
        message(FATAL_ERROR
            "Boost headers used by standalone SMESH were not found at ${_smesh_boost_root}. "
            "Build the backend under libs/boost_1_84_0 or set CAE_SMESH_BOOST_ROOT.")
    endif()

    file(GLOB _smesh_include_children LIST_DIRECTORIES true "${_smesh_inc}/*")
    set(_smesh_include_dirs "${_smesh_inc}")
    foreach(_item IN LISTS _smesh_include_children)
        if(IS_DIRECTORY "${_item}")
            list(APPEND _smesh_include_dirs "${_item}")
        endif()
    endforeach()

    set(_smesh_components
        NETGENPlugin nglib4smesh StdMeshers SMESH SMESHDS SMDS SMESHUtils Controls MEFISTO2
        Driver DriverDAT DriverGMF DriverSTL DriverUNV Geom Kernel)
    set(_smesh_libraries)
    foreach(_component IN LISTS _smesh_components)
        if(NOT EXISTS "${_smesh_lib}/${_component}.lib")
            message(FATAL_ERROR "Standalone SMESH library missing: ${_smesh_lib}/${_component}.lib")
        endif()
        list(APPEND _smesh_libraries "${_smesh_lib}/${_component}.lib")
    endforeach()

    add_library(CAE_SMESHBridge SHARED
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../tools/SMESHBridgeApi.h"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../tools/SMESHBridge.cpp")
    set_target_properties(CAE_SMESHBridge PROPERTIES
        AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
        OUTPUT_NAME "CAE_SMESHBridge"
        RUNTIME_OUTPUT_DIRECTORY "${CAE_PLUGIN_DIR}"
        LIBRARY_OUTPUT_DIRECTORY "${CAE_PLUGIN_DIR}"
        ARCHIVE_OUTPUT_DIRECTORY "${CAE_LIB_DIR}")
    foreach(_config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
        set_target_properties(CAE_SMESHBridge PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY_${_config} "${CAE_PLUGIN_DIR}"
            LIBRARY_OUTPUT_DIRECTORY_${_config} "${CAE_PLUGIN_DIR}"
            ARCHIVE_OUTPUT_DIRECTORY_${_config} "${CAE_LIB_DIR}")
    endforeach()
    target_compile_features(CAE_SMESHBridge PRIVATE cxx_std_17)
    target_compile_definitions(CAE_SMESHBridge PRIVATE
        CAE_SMESH_BRIDGE_BUILD=1 SALOME_USE_64BIT_IDS=1
        _CRT_SECURE_NO_WARNINGS NOMINMAX)
    target_include_directories(CAE_SMESHBridge PRIVATE
        ${_smesh_include_dirs}
        "${_smesh_boost_root}"
        "${CAE_SMESH_PTHREAD_ROOT}/Library/include"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../tools")
    target_link_libraries(CAE_SMESHBridge PRIVATE
        ${_smesh_libraries} CAE::OCC CAE::VTK)
    if(MSVC)
        target_compile_options(CAE_SMESHBridge PRIVATE /utf-8 /bigobj)
    endif()
    if(COMMAND cae_vtk_autoinit)
        cae_vtk_autoinit(CAE_SMESHBridge)
    endif()

    file(GLOB _smesh_runtime_dlls CONFIGURE_DEPENDS "${_smesh_bin}/*.dll")
    file(GLOB _smesh_boost_dlls "${_smesh_boost_root}/stage/lib/boost_*.dll")
    file(GLOB _smesh_pthread_dlls "${CAE_SMESH_PTHREAD_ROOT}/Library/bin/*.dll")
    # A rebuilt SDK DLL can retain an identical import library. Without a
    # dependency on the actual runtime, Ninja may skip linking this bridge and
    # therefore skip POST_BUILD deployment, leaving the application on an old
    # NETGENPlugin binary while headers/libraries already belong to the new SDK.
    set_property(TARGET CAE_SMESHBridge APPEND PROPERTY LINK_DEPENDS
        ${_smesh_runtime_dlls} ${_smesh_boost_dlls} ${_smesh_pthread_dlls})
    add_custom_command(TARGET CAE_SMESHBridge POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${_smesh_runtime_dlls} ${_smesh_boost_dlls} ${_smesh_pthread_dlls}
            "${CMAKE_BINARY_DIR}"
        VERBATIM)

    # Earlier probe builds deployed dependencies beside the bridge. Keep any
    # such copies synchronized, otherwise Windows may load the stale DLL there
    # before finding the current application-directory runtime.
    foreach(_dll IN LISTS _smesh_runtime_dlls)
        get_filename_component(_dll_name "${_dll}" NAME)
        if(EXISTS "${CAE_PLUGIN_DIR}/${_dll_name}")
            add_custom_command(TARGET CAE_SMESHBridge POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_dll}"
                    "${CAE_PLUGIN_DIR}/${_dll_name}"
                VERBATIM)
        endif()
    endforeach()

    if(EXISTS "${CAE_SMESH_ROOT}/LICENSE.txt")
        add_custom_command(TARGET CAE_SMESHBridge POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:CAE_SMESHBridge>/licenses"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CAE_SMESH_ROOT}/LICENSE.txt"
                "$<TARGET_FILE_DIR:CAE_SMESHBridge>/licenses/standalone-SMESH-LICENSE.txt"
            VERBATIM)
    endif()

    add_dependencies(${mesh_target} CAE_SMESHBridge)
    foreach(_license Netgen-LICENSE.txt NETGENPlugin-COPYING.txt)
        if(EXISTS "${CAE_SMESH_ROOT}/${_license}")
            add_custom_command(TARGET CAE_SMESHBridge POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CAE_SMESH_ROOT}/${_license}"
                    "$<TARGET_FILE_DIR:CAE_SMESHBridge>/licenses/${_license}"
                VERBATIM)
        endif()
    endforeach()
    target_compile_definitions(${mesh_target} PRIVATE CAE_SMESH_DLL_BACKEND=1)
    message(STATUS "Standalone SMESH in-process DLL backend enabled: ${CAE_SMESH_ROOT}")
endfunction()
