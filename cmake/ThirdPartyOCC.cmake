include_guard(GLOBAL)

if(NOT EXISTS "${CAE_OCC_ROOT}/inc")
    message(FATAL_ERROR "OCC include directory not found: ${CAE_OCC_ROOT}/inc")
endif()

if(NOT EXISTS "${CAE_OCC_ROOT}/lib")
    message(FATAL_ERROR "OCC lib directory not found: ${CAE_OCC_ROOT}/lib")
endif()

set(CAE_OCC_LIB_NAMES
    TKBin
    TKBinL
    TKBinTObj
    TKBinXCAF
    TKBO
    TKBool
    TKBRep
    TKCAF
    TKCDF
    TKDCAF
    TKDraw
    TKernel
    TKFeat
    TKFillet
    TKG2d
    TKG3d
    TKGeomAlgo
    TKGeomBase
    TKHLR
    TKIGES
    TKIVtk
    TKIVtkDraw
    TKLCAF
    TKMath
    TKMesh
    TKMeshVS
    TKOffset
    TKOpenGl
    TKOpenGlTest
    TKPrim
    TKQADraw
    TKRWMesh
    TKService
    TKShHealing
    TKStd
    TKStdL
    TKSTEP
    TKSTEP209
    TKSTEPAttr
    TKSTEPBase
    TKSTL
    TKTObj
    TKTObjDRAW
    TKTopAlgo
    TKTopTest
    TKV3d
    TKVCAF
    TKVRML
    TKXCAF
    TKXDEDRAW
    TKXDEIGES
    TKXDESTEP
    TKXMesh
    TKXml
    TKXmlL
    TKXmlTObj
    TKXmlXCAF
    TKXSBase
    TKXSDRAW
)

file(STRINGS "${CAE_OCC_ROOT}/inc/Standard_Version.hxx" _occ_version_line
    REGEX "^#define OCC_VERSION_COMPLETE[ \t]+")
string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" CAE_OCC_VERSION "${_occ_version_line}")
if(CAE_OCC_VERSION VERSION_GREATER_EQUAL "7.8.0")
    # OCCT 7.8 consolidated STEP/IGES/STL toolkits under the TKDE names.
    set(CAE_OCC_LIB_NAMES
        TKernel TKMath TKG2d TKG3d TKBRep TKGeomBase TKGeomAlgo TKTopAlgo
        TKPrim TKBO TKBool TKFeat TKFillet TKOffset TKShHealing TKMesh TKMeshVS
        TKService TKV3d TKOpenGl TKIVtk
        TKCDF TKLCAF TKCAF TKVCAF TKXCAF TKBinL TKBin TKBinXCAF TKXmlL TKXml TKXmlXCAF
        TKXSBase TKDE TKDESTEP TKDEIGES TKDESTL TKRWMesh)
endif()
message(STATUS "CAE shared OpenCASCADE ${CAE_OCC_VERSION}: ${CAE_OCC_ROOT}")

set(CAE_OCC_LIBRARIES "")

foreach(_occ_lib IN LISTS CAE_OCC_LIB_NAMES)
    # Switching the package must not retain import libraries from the old root.
    if(CAE_OCC_${_occ_lib}_LIBRARY AND NOT CAE_OCC_${_occ_lib}_LIBRARY STREQUAL "${CAE_OCC_ROOT}/lib/${_occ_lib}.lib")
        unset(CAE_OCC_${_occ_lib}_LIBRARY CACHE)
    endif()
    find_library(CAE_OCC_${_occ_lib}_LIBRARY
        NAMES ${_occ_lib}
        PATHS "${CAE_OCC_ROOT}/lib"
        NO_DEFAULT_PATH
    )

    if(NOT CAE_OCC_${_occ_lib}_LIBRARY)
        message(FATAL_ERROR "OCC library not found: ${_occ_lib} under ${CAE_OCC_ROOT}/lib")
    endif()

    list(APPEND CAE_OCC_LIBRARIES "${CAE_OCC_${_occ_lib}_LIBRARY}")
endforeach()

add_library(CAE::OCC INTERFACE IMPORTED GLOBAL)

set_target_properties(CAE::OCC PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${CAE_OCC_ROOT}/inc"
    INTERFACE_LINK_LIBRARIES "${CAE_OCC_LIBRARIES}"
    INTERFACE_COMPILE_DEFINITIONS "$<$<CONFIG:Debug>:_DEBUG>"
    INTERFACE_COMPILE_FEATURES cxx_std_11
)

function(cae_copy_occ_runtime target)
    if(WIN32)
        file(GLOB _occ_runtime_dlls "${CAE_OCC_ROOT}/bin/*.dll")
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${_occ_runtime_dlls} "${CAE_RUNTIME_DIR}"
            VERBATIM)
    endif()
endfunction()
