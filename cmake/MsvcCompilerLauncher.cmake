# MSVC uses the Windows ANSI code page for diagnostics even with /utf-8 and
# VSLANG=1033 when the English language pack is absent. Normalize include lines
# to ASCII before Ninja parses them. Keep compiler arguments and errors intact.
set(_arguments)
set(_after_separator FALSE)
math(EXPR _last "${CMAKE_ARGC} - 1")
foreach(_index RANGE 0 ${_last})
    if(_after_separator)
        string(REPLACE ";" "\\;" _argument "${CMAKE_ARGV${_index}}")
        # CMake lists cannot represent an element ending in a backslash: it
        # escapes the following separator. MSVC accepts / in output paths.
        if(_argument MATCHES "^/[Ff][odpe].*\\\\$")
            string(REGEX REPLACE "\\\\$" "/" _argument "${_argument}")
        endif()
        list(APPEND _arguments "${_argument}")
    elseif(CMAKE_ARGV${_index} STREQUAL "--")
        set(_after_separator TRUE)
    endif()
endforeach()
execute_process(COMMAND ${_arguments}
    RESULT_VARIABLE _status OUTPUT_VARIABLE _output ERROR_VARIABLE _error ENCODING OEM)
foreach(_stream _output _error)
    string(REGEX REPLACE "(^|[\r\n])[^:\r\n]+: [^:\r\n]+: +([A-Za-z]:[^\r\n]*)"
        "\\1Note: including file: \\2" ${_stream} "${${_stream}}")
    if(NOT "${${_stream}}" STREQUAL "")
        message("${${_stream}}")
    endif()
endforeach()
if(NOT _status EQUAL 0)
    message(FATAL_ERROR "MSVC compiler exited with ${_status}")
endif()
