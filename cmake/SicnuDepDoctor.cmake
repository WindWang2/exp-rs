# SicnuDepDoctor.cmake — configure-time dependency doctor.
#
# Complements (and never replaces) the runtime env-doctor from Deployment 11.0
# (src/geospatial/doctor/env_doctor.*): that one audits a DEPLOYED bundle at
# first run; this one makes the BUILD's own dependency discovery diagnosable.
#
# Two facilities:
#
#   sicnu_require_dependency(<name> [REQUIRED] [args...] [VERSION <x[.y]>])
#     Wraps find_package for hard dependencies. On failure the configure stops
#     with an actionable message: the package, what was searched, and the exact
#     install command per platform (apt / pacman / dnf / brew / vcpkg) — never
#     CMake's bare "Could NOT find X" wall of paths, and never a deep link error.
#     On success it records the found version/mode/path for the summary.
#
#   sicnu_require_optional_dependency(<name> [args...])
#     Same recording for QUIET packages: never fails, records mode.
#
#   sicnu_dependency_summary()
#     Prints the end-of-configure dependency block (versions, CONFIG/MODULE
#     mode, resolved paths, vcpkg target triplet). Paste it into bug reports.
#
# Design constraints:
#   * Zero behavioural change to what is searched or found — only the failure
#     text, the recorded metadata and the summary block are added.
#   * Minimal supported versions are declared by the call sites (this module
#     never invents version floors; the project minimums live in CMakeLists).

# ---------------------------------------------------------------------------
# Per-platform install hints. One row per dependency:
#   sicnu_dep_hints(<PkgVar>) -> "apt:pkg|pacman:pkg|brew:pkg|vcpkg:pkg" pieces
# Kept as a flat map so a missing entry is a loud, fixable omission.
# ---------------------------------------------------------------------------
set(_SICNU_DEP_HINT_Qt6        "qt6-base-dev|qt6-base|qt|qt6-base")
set(_SICNU_DEP_HINT_GDAL       "libgdal-dev|gdal|gdal|gdal")
set(_SICNU_DEP_HINT_PROJ       "libproj-dev|proj|proj|proj")
set(_SICNU_DEP_HINT_GEOS       "libgeos-dev|geos|geos|geos")
set(_SICNU_DEP_HINT_Protobuf   "libprotobuf-dev|protobuf|protobuf|protobuf")
set(_SICNU_DEP_HINT_LibZip     "libzip-dev|libzip|libzip|libzip")
set(_SICNU_DEP_HINT_EXPAT      "libexpat1-dev|expat|expat|expat")
set(_SICNU_DEP_HINT_SQLite3    "libsqlite3-dev|sqlite|sqlite|sqlite3")
set(_SICNU_DEP_HINT_ZLIB       "zlib1g-dev|zlib|zlib|zlib")
set(_SICNU_DEP_HINT_ZSTD       "libzstd-dev|zstd|zstd|zstd")
set(_SICNU_DEP_HINT_jsoncpp    "libjsoncpp-dev|jsoncpp|jsoncpp|jsoncpp")
set(_SICNU_DEP_HINT_QCA        "qca-qt6|qca|qca|qca")
set(_SICNU_DEP_HINT_Qt6Keychain "qtkeychain-qt6|qtkeychain|qtkeychain|qtkeychain")
set(_SICNU_DEP_HINT_OpenCV     "libopencv-dev|opencv|opencv|opencv4")
set(_SICNU_DEP_HINT_GSL        "libgsl-dev|gsl|gsl|gsl")
set(_SICNU_DEP_HINT_BISON      "bison|bison|bison|bison")
set(_SICNU_DEP_HINT_FLEX       "flex|flex|flex|flex")
set(_SICNU_DEP_HINT_CURL       "libcurl4-openssl-dev|curl|curl|curl")
set(_SICNU_DEP_HINT_PCRE2      "libpcre2-dev|pcre2|pcre2|pcre2")
set(_SICNU_DEP_HINT_NetCDF     "libnetcdf-dev|netcdf|netcdf|netcdf")
set(_SICNU_DEP_HINT_Boost      "libboost-dev|boost|boost|boost")

# Collected dependency records for sicnu_dep_note()/summary.
set(SICNU_DEP_DOCTOR_RECORDS "" CACHE INTERNAL "dependency doctor records" FORCE)

# sicnu_dep_pkg_manager() -> "apt" | "pacman" | "dnf" | "brew" | "unknown"
function(sicnu_dep_pkg_manager out_var)
    if(APPLE)
        set(${out_var} "brew" PARENT_SCOPE)
    elseif(EXISTS "/etc/os-release")
        file(READ "/etc/os-release" _osrel)
        string(TOLOWER "${_osrel}" _osrel_l)
        if(_osrel_l MATCHES "id=ubuntu|id=debian|id_like=.*debian")
            set(${out_var} "apt" PARENT_SCOPE)
        elseif(_osrel_l MATCHES "id=arch|id_like=.*arch")
            set(${out_var} "pacman" PARENT_SCOPE)
        elseif(_osrel_l MATCHES "id=fedora|id_like=.*fedora")
            set(${out_var} "dnf" PARENT_SCOPE)
        else()
            set(${out_var} "unknown" PARENT_SCOPE)
        endif()
    else()
        set(${out_var} "unknown" PARENT_SCOPE)
    endif()
endfunction()

# sicnu_dep_hint_lines(<PkgName> out_var): the multi-line, platform-specific
# "how to fix" block for one package.
function(sicnu_dep_hint_lines pkg out_var)
    if(NOT DEFINED _SICNU_DEP_HINT_${pkg})
        set(${out_var} "" PARENT_SCOPE)
        return()
    endif()
    string(REPLACE "|" ";" _h "${_SICNU_DEP_HINT_${pkg}}")
    list(LENGTH _h _hlen)
    if(NOT _hlen EQUAL 4)
        set(${out_var} "" PARENT_SCOPE)
        return()
    endif()
    list(GET _h 0 _apt)
    list(GET _h 1 _pacman)
    list(GET _h 2 _brew)
    list(GET _h 3 _vcpkg)
    sicnu_dep_pkg_manager(_mgr)
    set(_lines "")
    if(_mgr STREQUAL "apt")
        set(_lines "${_apt} (apt)")
    elseif(_mgr STREQUAL "pacman")
        set(_lines "${_pacman} (pacman)")
    elseif(_mgr STREQUAL "dnf")
        set(_lines "dnf install ${_apt%-dev} (dnf)")
    elseif(_mgr STREQUAL "brew")
        set(_lines "${_brew} (brew)")
    endif()
    if(_mgr STREQUAL "apt")
        set(_lines "${_lines}\n    macOS: brew install ${_brew}\n    Arch: pacman -S ${_pacman}\n    vcpkg: vcpkg install ${_vcpkg}")
    else()
        set(_lines "${_lines}\n    Ubuntu/Debian: apt install ${_apt}\n    macOS: brew install ${_brew}\n    Arch: pacman -S ${_pacman}\n    vcpkg: vcpkg install ${_vcpkg}")
    endif()
    set(${out_var} "${_lines}" PARENT_SCOPE)
endfunction()

# sicnu_dep_note(<name> [FOUND_VERSION <v>] [MODE <config|module|absent>] [PATH <p>])
# Records one row for the end-of-configure summary (last row per name wins,
# so re-configures never duplicate a package). Output: none.
function(sicnu_dep_note name)
    cmake_parse_arguments(A "" "FOUND_VERSION;MODE;PATH" "" ${ARGN})
    set(_row "${name}|${A_FOUND_VERSION}|${A_MODE}|${A_PATH}")
    set(_kept "")
    foreach(_r ${SICNU_DEP_DOCTOR_RECORDS})
        if(_r STREQUAL "")
            continue()
        endif()
        string(REPLACE "|" ";" _f "${_r}")
        list(GET _f 0 _rn)
        if(NOT _rn STREQUAL name)
            list(APPEND _kept "${_r}")
        endif()
    endforeach()
    list(APPEND _kept "${_row}")
    set(SICNU_DEP_DOCTOR_RECORDS "${_kept}" CACHE INTERNAL "dependency doctor records" FORCE)
endfunction()

# sicnu_require_dependency(<name> [MINIMUM_VERSION <v>] [CONFIG|MODULE] <find_package args...>)
#
# MACRO, not function, and that is load-bearing: vcpkg's find_package override
# is itself a macro, and repo find modules (e.g. cmake/FindSqlite3.cmake)
# enforce "run once" through ordinary variables that must survive in the
# DIRECTORY scope. A function wrapper would confine those variables to the
# wrapper's scope (lost on return), so a nested vcpkg-driven discovery followed
# by an explicit call trips the module's guard. Expanding in the caller's scope
# makes this behave exactly like the plain find_package it replaces.
# All temporaries are _sicnu_dep_-prefixed and unset before the macro ends.
macro(sicnu_require_dependency name)
    set(_sicnu_dep_min "")
    set(_sicnu_dep_fp "")
    set(_sicnu_dep_expect_min FALSE)
    foreach(_sicnu_dep_a ${ARGN})
        if(_sicnu_dep_a STREQUAL "REQUIRED")
            # Consumed here: CMake's own REQUIRED failure would fire before
            # this module could print the actionable diagnosis.
        elseif(_sicnu_dep_a STREQUAL "MINIMUM_VERSION")
            set(_sicnu_dep_expect_min TRUE)
        elseif(_sicnu_dep_expect_min)
            set(_sicnu_dep_min "${_sicnu_dep_a}")
            set(_sicnu_dep_expect_min FALSE)
        else()
            list(APPEND _sicnu_dep_fp "${_sicnu_dep_a}")
        endif()
    endforeach()
    find_package(${name} ${_sicnu_dep_min} ${_sicnu_dep_fp})
    if(${name}_FOUND)
        set(_sicnu_dep_ver "${${name}_VERSION}")
        if(NOT _sicnu_dep_ver AND DEFINED ${name}_VERSION_STRING)
            set(_sicnu_dep_ver "${${name}_VERSION_STRING}")
        endif()
        set(_sicnu_dep_path "")
        if(DEFINED ${name}_DIR)
            set(_sicnu_dep_path "${${name}_DIR}")
        endif()
        sicnu_dep_note(${name} FOUND_VERSION "${_sicnu_dep_ver}" MODE "config" PATH "${_sicnu_dep_path}")
    else()
        sicnu_dep_hint_lines(${name} _sicnu_dep_hints)
        set(_sicnu_dep_msg "Could NOT find ${name} (required by this project).")
        if(_sicnu_dep_min)
            set(_sicnu_dep_msg "${_sicnu_dep_msg} Minimum version: ${_sicnu_dep_min}.")
        endif()
        if(DEFINED ${name}_VERSION)
            set(_sicnu_dep_msg "${_sicnu_dep_msg} Found version: ${${name}_VERSION} (too old or incomplete).")
        endif()
        if(_sicnu_dep_hints)
            set(_sicnu_dep_msg "${_sicnu_dep_msg}\n  install:\n    ${_sicnu_dep_hints}")
        else()
            set(_sicnu_dep_msg "${_sicnu_dep_msg}\n  (no install hint registered for ${name} — add one to cmake/SicnuDepDoctor.cmake)")
        endif()
        if(DEFINED ${name}_DIR)
            set(_sicnu_dep_msg "${_sicnu_dep_msg}\n  searched ${name}_DIR=${${name}_DIR}; override with -D${name}_DIR=<path> or -DCMAKE_PREFIX_PATH=<prefix>.")
        endif()
        sicnu_dep_note(${name} MODE "absent" PATH "")
        message(FATAL_ERROR "${_sicnu_dep_msg}")
    endif()
    unset(_sicnu_dep_min)
    unset(_sicnu_dep_fp)
    unset(_sicnu_dep_expect_min)
    unset(_sicnu_dep_ver)
    unset(_sicnu_dep_path)
    unset(_sicnu_dep_hints)
    unset(_sicnu_dep_msg)
    unset(_sicnu_dep_a)
endmacro()

# sicnu_require_optional_dependency(<name> <find_package args...>)
# Same scope discipline (macro); QUIET by construction: records found/absent,
# never fails.
macro(sicnu_require_optional_dependency name)
    find_package(${name} QUIET ${ARGN})
    if(${name}_FOUND)
        set(_sicnu_dep_ver "${${name}_VERSION}")
        if(NOT _sicnu_dep_ver AND DEFINED ${name}_VERSION_STRING)
            set(_sicnu_dep_ver "${${name}_VERSION_STRING}")
        endif()
        set(_sicnu_dep_path "")
        if(DEFINED ${name}_DIR)
            set(_sicnu_dep_path "${${name}_DIR}")
        endif()
        sicnu_dep_note(${name} FOUND_VERSION "${_sicnu_dep_ver}" MODE "config" PATH "${_sicnu_dep_path}")
    else()
        sicnu_dep_note(${name} MODE "absent" PATH "")
    endif()
    unset(_sicnu_dep_ver)
    unset(_sicnu_dep_path)
endmacro()

# sicnu_dependency_summary(): the single block paste-able into bug reports.
function(sicnu_dependency_summary)
    message(STATUS "=== SICNU dependency summary ===")
    foreach(_row ${SICNU_DEP_DOCTOR_RECORDS})
        if(_row STREQUAL "")
            continue()
        endif()
        string(REPLACE "|" ";" _f "${_row}")
        list(GET _f 0 _n)
        list(GET _f 1 _v)
        list(GET _f 2 _m)
        list(GET _f 3 _p)
        if(_m STREQUAL "absent")
            message(STATUS "  ${_n}: MISSING")
        elseif(_v STREQUAL "")
            message(STATUS "  ${_n}: found (${_m}) ${_p}")
        else()
            message(STATUS "  ${_n}: ${_v} (${_m}) ${_p}")
        endif()
    endforeach()
    if(CMAKE_TOOLCHAIN_FILE MATCHES "vcpkg" OR DEFINED VCPKG_TARGET_TRIPLET)
        message(STATUS "  vcpkg: ${VCPKG_TARGET_TRIPLET} (installed: ${VCPKG_INSTALLED_DIR})")
    endif()
    if(SICNU_HAS_OTB)
        message(STATUS "  OTB/ITK: vendored build enabled (SICNU_BUILD_OTB=ON)")
    endif()
    message(STATUS "  NetCDF: provided through GDAL drivers (runtime audit: sicnu_geo_rs_cli env-doctor)")
    message(STATUS "=== end dependency summary ===")
endfunction()
