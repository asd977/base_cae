# Ninja must match /showIncludes output byte-for-byte. Localized MSVC output
# can use the Windows code page while CMake writes its prefix as UTF-8, leaving
# apparently successful builds with zero header dependencies and mixed ABIs.
# Normalize include diagnostics to ASCII before Ninja reads them. VSLANG alone
# is insufficient on installations without an English language pack.
if(MSVC AND CMAKE_GENERATOR MATCHES "Ninja")
    set(_cae_msvc_launcher "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_LIST_DIR}/MsvcCompilerLauncher.cmake" --)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/CaeIncludeDependencyProbe.h" "// Header dependency probe\n")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/CaeIncludeDependencyProbe.cpp"
        "#include \"CaeIncludeDependencyProbe.h\"\n")
    execute_process(COMMAND ${_cae_msvc_launcher}
        "${CMAKE_CXX_COMPILER}" /nologo /showIncludes /EP
        "${CMAKE_CURRENT_BINARY_DIR}/CaeIncludeDependencyProbe.cpp"
        OUTPUT_VARIABLE _cae_include_output ERROR_VARIABLE _cae_include_error
        RESULT_VARIABLE _cae_include_status ENCODING UTF-8)
    string(REGEX MATCH "([^\r\n]+:) +[^\r\n]*CaeIncludeDependencyProbe.h"
        _cae_include_match "${_cae_include_output}\n${_cae_include_error}")
    if(NOT _cae_include_status EQUAL 0 OR NOT _cae_include_match)
        message(FATAL_ERROR "Cannot determine the MSVC /showIncludes dependency prefix")
    endif()
    string(STRIP "${CMAKE_MATCH_1}" CMAKE_CL_SHOWINCLUDES_PREFIX)
    set(CMAKE_CXX_CL_SHOWINCLUDES_PREFIX "${CMAKE_CL_SHOWINCLUDES_PREFIX}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${_cae_msvc_launcher}")
    unset(_cae_msvc_launcher)
endif()
