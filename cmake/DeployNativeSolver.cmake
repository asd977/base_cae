# Copy only the executable and the DLLs it loads, never compiler tools or objects.
if(NOT EXISTS "${CAE_SOLVER_SOURCE}" OR NOT CAE_SOLVER_DESTINATION)
    message(FATAL_ERROR "A valid CAE_SOLVER_SOURCE and CAE_SOLVER_DESTINATION are required")
endif()
if(NOT EXISTS "${CAE_SOLVER_DUMPBIN}")
    message(FATAL_ERROR "dumpbin is required to resolve native solver DLL dependencies")
endif()
set(CMAKE_GET_RUNTIME_DEPENDENCIES_PLATFORM "windows+pe")
set(CMAKE_GET_RUNTIME_DEPENDENCIES_TOOL "dumpbin")
set(CMAKE_GET_RUNTIME_DEPENDENCIES_COMMAND "${CAE_SOLVER_DUMPBIN}")
get_filename_component(_source_dir "${CAE_SOLVER_SOURCE}" DIRECTORY)
get_filename_component(_destination_dir "${CAE_SOLVER_DESTINATION}" DIRECTORY)
file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES "${CAE_SOLVER_SOURCE}"
    DIRECTORIES "${_source_dir}" ${CAE_SOLVER_DEPENDENCY_DIRS}
    RESOLVED_DEPENDENCIES_VAR _dependencies
    UNRESOLVED_DEPENDENCIES_VAR _missing
    PRE_EXCLUDE_REGEXES "^[Aa][Pp][Ii]-[Mm][Ss]-" "^[Ee][Xx][Tt]-[Mm][Ss]-"
    POST_EXCLUDE_REGEXES ".*[/\\][Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\].*")
if(_missing)
    message(FATAL_ERROR "Native solver runtime dependencies are missing: ${_missing}")
endif()
file(MAKE_DIRECTORY "${_destination_dir}")
set(_sources "${CAE_SOLVER_SOURCE}" ${_dependencies})
foreach(_file IN LISTS _sources)
    if(_file STREQUAL CAE_SOLVER_SOURCE)
        set(_destination "${CAE_SOLVER_DESTINATION}")
    else()
        get_filename_component(_name "${_file}" NAME)
        set(_destination "${_destination_dir}/${_name}")
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_file}" "${_destination}"
        RESULT_VARIABLE _status)
    if(NOT _status EQUAL 0)
        message(FATAL_ERROR "Failed to deploy native solver file: ${_file}")
    endif()
endforeach()
file(SHA256 "${CAE_SOLVER_SOURCE}" _source_hash)
file(SHA256 "${CAE_SOLVER_DESTINATION}" _deployed_hash)
if(NOT _source_hash STREQUAL _deployed_hash)
    message(FATAL_ERROR "Deployed solver does not match source: ${CAE_SOLVER_DESTINATION}")
endif()
message(STATUS "Deployed ${CAE_SOLVER_DESTINATION} (SHA256 ${_deployed_hash})")
