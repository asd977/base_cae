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

set(CAE_OCC_LIBRARIES "")

foreach(_occ_lib IN LISTS CAE_OCC_LIB_NAMES)
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
