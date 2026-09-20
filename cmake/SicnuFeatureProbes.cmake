# SicnuFeatureProbes.cmake — portable feature detection by COMPILE CHECK.
#
# #1108's lesson: version-number ifdefs guess wrong (GDAL 3.8.4 lacks the
# single-argument CPLErrorStateBackuper constructor that 3.9 added, and the
# version range tables did not know it). Compile checks against the headers we
# actually found are the only portable truth, and they give the
# "fails-before / passes-after" property the tests require.
#
# Contract:
#   sicnu_feature_probe(<SICNU_HAVE_X> "<description>"
#                       [INCLUDES "..."] [CODE "..."] [DEPENDS <target>...])
#     * DEPENDS unsatisfied (target missing) -> probe skipped, macro = 0,
#       status note. Never a configure error.
#     * otherwise check_cxx_source_compiles runs; macro = 1/0 accordingly.
#     * the result is a CACHE var AND a line in the generated header
#       ${CMAKE_BINARY_DIR}/sicnu_feature_probes.h, where every macro is
#       ALWAYS defined (0 or 1) — consumers can #if without #ifdef dances.
#
#   sicnu_feature_probes_report()
#     Prints the outcome of every probe into the configure log.
#
# Consumers must never re-derive these from version numbers; the Catch2
# contract test (tests/test_feature_probes.cpp) asserts the generated header
# is complete and cross-checks the two highest-signal flags at runtime.

include(CheckCXXSourceCompiles)

set(SICNU_FEATURE_PROBES "" CACHE INTERNAL "feature probe records" FORCE)

# sicnu_feature_probe(<macro> "<description>"
#                     [INCLUDES "<code>"] [CODE "<code>"]
#                     [REQUIRED_INCLUDES "..."] [REQUIRED_LIBRARIES "..."]
#                     [REQUIRED_FLAGS "..."] [DEPENDS t...])
# Macro (scope discipline: see SicnuDepDoctor.cmake — the compile-check cache
# variables must survive in the caller's directory scope).
macro(sicnu_feature_probe macro_name description)
    # Manual keyword parse (cmake_parse_arguments cannot hold semicolon-bearing
    # LIST values such as REQUIRED_INCLUDES from a multi-directory Qt install:
    # the list splits into separate arguments and the tail is dropped). Values
    # for REQUIRED_INCLUDES / REQUIRED_LIBRARIES / DEPENDS are collected until
    # the next keyword and re-joined with ';'. INCLUDES / CODE take exactly one
    # value, which must not itself contain semicolons - write CODE as a single
    # statement without its trailing ';' (the template wraps it in a block and
    # appends the ';').
    set(_sfp_inc "")
    set(_sfp_code "")
    set(_sfp_flags "")
    set(_sfp_ri "")
    set(_sfp_rl "")
    set(_sfp_deps "")
    set(_sfp_mode "")
    foreach(_sfp_a ${ARGN})
        if(_sfp_a STREQUAL "INCLUDES")
            set(_sfp_mode "inc")
        elseif(_sfp_a STREQUAL "CODE")
            set(_sfp_mode "code")
        elseif(_sfp_a STREQUAL "REQUIRED_FLAGS")
            set(_sfp_mode "flags")
        elseif(_sfp_a STREQUAL "REQUIRED_INCLUDES")
            set(_sfp_mode "ri")
        elseif(_sfp_a STREQUAL "REQUIRED_LIBRARIES")
            set(_sfp_mode "rl")
        elseif(_sfp_a STREQUAL "DEPENDS")
            set(_sfp_mode "deps")
        elseif(_sfp_mode STREQUAL "inc")
            set(_sfp_inc "${_sfp_a}")
            set(_sfp_mode "")
        elseif(_sfp_mode STREQUAL "code")
            set(_sfp_code "${_sfp_a}")
            set(_sfp_mode "")
        elseif(_sfp_mode STREQUAL "flags")
            set(_sfp_flags "${_sfp_a}")
            set(_sfp_mode "")
        elseif(_sfp_mode STREQUAL "ri")
            if(_sfp_ri STREQUAL "")
                set(_sfp_ri "${_sfp_a}")
            else()
                set(_sfp_ri "${_sfp_ri};${_sfp_a}")
            endif()
        elseif(_sfp_mode STREQUAL "rl")
            if(_sfp_rl STREQUAL "")
                set(_sfp_rl "${_sfp_a}")
            else()
                set(_sfp_rl "${_sfp_rl};${_sfp_a}")
            endif()
        elseif(_sfp_mode STREQUAL "deps")
            list(APPEND _sfp_deps "${_sfp_a}")
        else()
            message(FATAL_ERROR
                "sicnu_feature_probe(${macro_name}): unexpected argument '${_sfp_a}' "
                "(values containing ';' cannot be passed; use comma expressions in CODE)")
        endif()
    endforeach()
    set(_sfp_ok TRUE)
    foreach(_sfp_dep ${_sfp_deps})
        if(NOT TARGET ${_sfp_dep})
            set(_sfp_ok FALSE)
        endif()
    endforeach()
    if(NOT _sfp_ok)
        message(STATUS "feature probe ${macro_name}: SKIPPED (dependency target missing)")
        set(${macro_name} 0 CACHE INTERNAL "feature probe skipped: ${description}")
        _sfp_record(${macro_name} "${description}" "skipped")
        return()
    endif()
    set(_sfp_src "${_sfp_inc}\nint main() { { ${_sfp_code}; } return 0; }\n")
    set(CMAKE_REQUIRED_QUIET TRUE)
    set(CMAKE_REQUIRED_FLAGS "${_sfp_flags}")
    set(CMAKE_REQUIRED_INCLUDES "${_sfp_ri}")
    set(CMAKE_REQUIRED_LIBRARIES "${_sfp_rl}")
    check_cxx_source_compiles("${_sfp_src}" ${macro_name})
    set(CMAKE_REQUIRED_FLAGS "")
    set(CMAKE_REQUIRED_INCLUDES "")
    set(CMAKE_REQUIRED_LIBRARIES "")
    if(${macro_name})
        message(STATUS "feature probe ${macro_name}: available — ${description}")
        _sfp_record(${macro_name} "${description}" "yes")
    else()
        message(STATUS "feature probe ${macro_name}: NOT available — ${description}")
        _sfp_record(${macro_name} "${description}" "no")
    endif()
endmacro()

# _sfp_record(<macro> "<description>" <yes|no|skipped>): append a record line.
function(_sfp_record macro_name description verdict)
    set(SICNU_FEATURE_PROBES
        "${SICNU_FEATURE_PROBES};${macro_name}|${verdict}|${description}"
        CACHE INTERNAL "" FORCE)
endfunction()

# sicnu_feature_probes_report(): summary block for the configure log.
function(sicnu_feature_probes_report)
    message(STATUS "=== SICNU feature probes ===")
    foreach(_row ${SICNU_FEATURE_PROBES})
        if(_row STREQUAL "")
            continue()
        endif()
        string(REPLACE "|" ";" _f "${_row}")
        list(GET _f 0 _n)
        list(GET _f 1 _v)
        list(GET _f 2 _d)
        message(STATUS "  ${_n}=${_v} — ${_d}")
    endforeach()
    message(STATUS "=== end feature probes ===")
endfunction()

# sicnu_write_feature_probes_header(<path>): regenerate the generated header so
# every probe macro is defined (0 or 1) for consumers and tests.
function(sicnu_write_feature_probes_header out_path)
    set(_body "")
    foreach(_row ${SICNU_FEATURE_PROBES})
        if(_row STREQUAL "")
            continue()
        endif()
        string(REPLACE "|" ";" _f "${_row}")
        list(GET _f 0 _n)
        list(GET _f 1 _v)
        list(GET _f 2 _d)
        if(_v STREQUAL "yes")
            set(_val 1)
        else()
            set(_val 0)
        endif()
        string(APPEND _body "#define ${_n} ${_val} /* ${_d} (${_v}) */\n")
    endforeach()
    file(WRITE "${out_path}"
"/* Generated by cmake/SicnuFeatureProbes.cmake — do not edit.
 * Every probe macro is ALWAYS defined (0 or 1); 0 also covers 'dependency not
 * present on this platform', so consumers can #if without #ifdef dances.
 */
${_body}")
    message(STATUS "feature probes header written: ${out_path}")
endfunction()
