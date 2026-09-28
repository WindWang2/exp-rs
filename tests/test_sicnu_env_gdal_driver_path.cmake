# test_sicnu_env_gdal_driver_path.cmake — policy unit + generated-artifact
# integration checks for the CTestCustom.cmake GDAL_DRIVER_PATH pin (#1355).
#
# Registered as a plain ctest script test (see tests/CMakeLists.txt): no C++
# binary is involved — the subject under test IS CMake code.
#
#  Policy unit checks (sicnu_gdal_driver_path_snippet):
#   - system /usr/lib* plugin dirs are excluded (no pin emitted);
#   - a prefix-built GDAL's plugin dir is pinned with prepend-or-set
#     semantics so a user GDAL_DRIVER_PATH survives (evaluated in this
#     process against both env states);
#   - paths with spaces/quotes survive the round-trip (cmake-escaping
#     contract for the CTestCustom.cmake text).
#
#  Generated-artifact checks (-DBUILD_DIR=<this build tree>):
#   - CTestCustom.cmake never contains a bare whole-value
#     `set(ENV{GDAL_DRIVER_PATH} "...")` overwrite — the only permitted
#     writes are the guarded prepend-or-set block;
#   - no emitted GDAL_DRIVER_PATH value lives under /usr/lib*.

include("${SRC_DIR}/cmake/SicnuTestEnv.cmake")

set(_failures "")

# ---------------------------------------------------------------------------
# 1. System prefixes must be excluded (#1355 acceptance: distro-GDAL machines
#    keep the user's environment untouched — no pin, no overwrite).
# ---------------------------------------------------------------------------
sicnu_gdal_driver_path_snippet("/usr/lib/x86_64-linux-gnu/gdalplugins" _s)
if(NOT _s STREQUAL "")
  list(APPEND _failures "system /usr/lib/<triple>/gdalplugins was pinned; must be excluded (got: ${_s})")
endif()

sicnu_gdal_driver_path_snippet("/usr/lib64/gdalplugins" _s)
if(NOT _s STREQUAL "")
  list(APPEND _failures "/usr/lib64 must be excluded by the ^/usr/lib.* policy (got: ${_s})")
endif()

sicnu_gdal_driver_path_snippet("" _s)
if(NOT _s STREQUAL "")
  list(APPEND _failures "empty plugin dir must produce no snippet (got: ${_s})")
endif()

# ---------------------------------------------------------------------------
# 2. Prefix-built GDAL: pin fires with prepend-or-set semantics. Execute the
#    generated snippet in this process for both runtime env states.
# ---------------------------------------------------------------------------
set(_prefix_dir "/opt/gdal-3.13/lib/gdalplugins")
sicnu_gdal_driver_path_snippet("${_prefix_dir}" _s)
if(_s STREQUAL "")
  list(APPEND _failures "prefix-built GDAL plugin dir must produce a pin snippet")
endif()

# 2a. Unset env → plain set.
unset(ENV{GDAL_DRIVER_PATH})
cmake_language(EVAL CODE "${_s}")
if(NOT "$ENV{GDAL_DRIVER_PATH}" STREQUAL "${_prefix_dir}")
  list(APPEND _failures "unset env: expected GDAL_DRIVER_PATH=${_prefix_dir}, got '$ENV{GDAL_DRIVER_PATH}'")
endif()

# 2b. User-provided value → prepended, user value carried (#1355 acceptance:
#     "用户合法 env 不被破坏").
set(ENV{GDAL_DRIVER_PATH} "/home/dev/my-gdal-plugins")
cmake_language(EVAL CODE "${_s}")
if(NOT "$ENV{GDAL_DRIVER_PATH}" STREQUAL "${_prefix_dir}:/home/dev/my-gdal-plugins")
  list(APPEND _failures "user GDAL_DRIVER_PATH clobbered instead of prepended: got '$ENV{GDAL_DRIVER_PATH}'")
endif()

# ---------------------------------------------------------------------------
# 3. Escaping contract: a path with spaces and quotes round-trips through the
#    generated code (the snippet is embedded into CTestCustom.cmake text).
# ---------------------------------------------------------------------------
set(_weird_dir "/opt/gda l-we\"ird")
_sicnu_escape_cmake_path(_weird_esc "${_weird_dir}/gdalplugins")
sicnu_gdal_driver_path_snippet("${_weird_esc}" _s)
unset(ENV{GDAL_DRIVER_PATH})
cmake_language(EVAL CODE "${_s}")
if(NOT "$ENV{GDAL_DRIVER_PATH}" STREQUAL "${_weird_dir}/gdalplugins")
  list(APPEND _failures "escaped path round-trip failed: got '$ENV{GDAL_DRIVER_PATH}'")
endif()

# ---------------------------------------------------------------------------
# 4. Generated-artifact integration checks on this build tree's
#    CTestCustom.cmake: whichever host layout configured the tree, the
#    generated file must never contain a bare whole-value GDAL_DRIVER_PATH
#    overwrite, and never pin a /usr/lib* plugin dir.
# ---------------------------------------------------------------------------
set(_custom "${BUILD_DIR}/CTestCustom.cmake")
if(NOT EXISTS "${_custom}")
  list(APPEND _failures "generated CTestCustom.cmake not found at ${_custom}")
else()
  file(STRINGS "${_custom}" _lines)
  # Per-line pin legality: every `set(ENV{GDAL_DRIVER_PATH} ...)` must be
  # either the prepend form (user value carried: `...:$ENV{GDAL_DRIVER_PATH}"`)
  # or the else-branch default set (user env was empty). Anything else is a
  # bare whole-value overwrite — the #1355 defect shape — wherever it appears
  # relative to the guard block.
  set(_prev_was_else OFF)
  set(_pin_seen OFF)
  foreach(_line IN LISTS _lines)
    if(_line MATCHES "^[ ]*else\\(\\)[ ]*$")
      set(_prev_was_else ON)
    else()
      if(_line MATCHES "set\\(ENV\\{GDAL_DRIVER_PATH\\}")
        set(_pin_seen ON)
        if(NOT _prev_was_else AND NOT _line MATCHES ":\\$ENV\\{GDAL_DRIVER_PATH\\}\"")
          list(APPEND _failures "bare GDAL_DRIVER_PATH overwrite (no prepend, no else-default) in generated CTestCustom.cmake: ${_line}")
        endif()
        if(_line MATCHES "\"/usr/lib")
          list(APPEND _failures "generated CTestCustom.cmake pins a /usr/lib* GDAL_DRIVER_PATH: ${_line}")
        endif()
      endif()
      set(_prev_was_else OFF)
    endif()
  endforeach()
  if(NOT _pin_seen)
    # Legal on a system-GDAL host (no pin generated at all); nothing more to
    # assert here — the unit checks above already cover the pin path.
    message(STATUS "CTestCustom.cmake carries no GDAL_DRIVER_PATH pin (system-GDAL host)")
  endif()
endif()

# ---------------------------------------------------------------------------
if(_failures)
  list(LENGTH _failures _n)
  foreach(_f IN LISTS _failures)
    message(SEND_ERROR "FAIL: ${_f}")
  endforeach()
  message(FATAL_ERROR "sicnu_env GDAL_DRIVER_PATH policy test: ${_n} failure(s)")
endif()
message(STATUS "sicnu_env GDAL_DRIVER_PATH policy test passed")
