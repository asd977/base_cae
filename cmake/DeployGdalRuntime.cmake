# Executed after linking, or against an existing deployment directory.
# Copy the DLL dependency closure instead of dumping the complete GDAL SDK bin.
if(NOT DEFINED CAE_GDAL_ROOT OR NOT DEFINED CAE_RUNTIME_DIR)
    message(FATAL_ERROR "CAE_GDAL_ROOT and CAE_RUNTIME_DIR are required")
endif()
set(_gdal "${CAE_GDAL_ROOT}/bin/gdal.dll")
if(NOT EXISTS "${_gdal}")
    message(FATAL_ERROR "GDAL runtime not found: ${_gdal}")
endif()

file(GET_RUNTIME_DEPENDENCIES
    LIBRARIES "${_gdal}"
    DIRECTORIES "${CAE_GDAL_ROOT}/bin"
    RESOLVED_DEPENDENCIES_VAR _dependencies
    UNRESOLVED_DEPENDENCIES_VAR _missing
    PRE_EXCLUDE_REGEXES "^[Aa][Pp][Ii]-[Mm][Ss]-" "^[Ee][Xx][Tt]-[Mm][Ss]-"
    POST_EXCLUDE_REGEXES ".*[/\\][Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\].*"
)
if(_missing)
    message(FATAL_ERROR "GDAL runtime dependencies are missing: ${_missing}")
endif()
file(MAKE_DIRECTORY "${CAE_RUNTIME_DIR}")
list(PREPEND _dependencies "${_gdal}")
foreach(_dll IN LISTS _dependencies)
    get_filename_component(_name "${_dll}" NAME)
    execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${_dll}" "${CAE_RUNTIME_DIR}/${_name}"
                    RESULT_VARIABLE _copy_result)
    if(NOT _copy_result EQUAL 0)
        message(FATAL_ERROR "Failed to deploy GDAL dependency: ${_dll}")
    endif()
endforeach()
foreach(_data IN ITEMS gdal proj)
    if(EXISTS "${CAE_GDAL_ROOT}/share/${_data}")
        file(COPY "${CAE_GDAL_ROOT}/share/${_data}/"
             DESTINATION "${CAE_RUNTIME_DIR}/${_data}-data")
    endif()
endforeach()
