# SicnuCatchAddTests — fault-tolerant wrapper around Catch2's PRE_TEST
# discovery (review P0-4).
#
# Catch2's CatchAddTests.cmake raises message(FATAL_ERROR) when a test
# executable crashes or exits non-zero while listing its tests. Under
# DISCOVERY_MODE PRE_TEST that runs inside ctest itself, so ONE broken
# binary (e.g. test_view_link segfaulting in static destruction) aborted the
# whole CTest run — even `ctest -N` could not list anything.
#
# This wrapper probes `<exe> --list-tests` first. On success it hands over to
# the stock Catch2 implementation unchanged. On failure it prints a warning
# and registers a single `<prefix><exe>_DISCOVERY_FAILED` test that re-runs
# the listing, so the breakage shows up as one failing test instead of an
# aborted run.
#
# Installed by tests/CMakeLists.txt by pointing Catch2's
# _CATCH_DISCOVER_TESTS_SCRIPT at this file; SICNU_CATCH_ADD_TESTS_UPSTREAM
# is substituted with the real CatchAddTests.cmake at configure time.

include("@SICNU_CATCH_ADD_TESTS_UPSTREAM@")

# Redefining a CMake command leaves the previous definition callable as
# `_<name>`, so after the include above `_catch_discover_tests_impl` is the
# stock Catch2 implementation (re-including this file per test binary keeps
# that invariant: upstream is re-defined first, then this wrapper).
# CMP0174 NEW: an empty keyword value (Catch2 passes many, e.g.
# `TEST_SUFFIX [==[]==]`) is an empty string, not a dev warning per binary.
# Policies are recorded when the function is defined.
cmake_policy(PUSH)
if(POLICY CMP0174)
  cmake_policy(SET CMP0174 NEW)
endif()
function(catch_discover_tests_impl)
  # PARSE_ARGV keeps empty values (e.g. `TEST_SUFFIX [==[]==]`) so they
  # cannot swallow the next keyword; the keyword list mirrors Catch2 v3.
  cmake_parse_arguments(
    PARSE_ARGV 0
    "_SICNU"
    ""
    "TEST_EXECUTABLE;TEST_WORKING_DIR;TEST_OUTPUT_DIR;TEST_OUTPUT_PREFIX;TEST_OUTPUT_SUFFIX;TEST_PREFIX;TEST_REPORTER;TEST_SPEC;TEST_SUFFIX;TEST_LIST;CTEST_FILE"
    "TEST_EXTRA_ARGS;TEST_PROPERTIES;TEST_EXECUTOR;TEST_DL_PATHS;TEST_DL_FRAMEWORK_PATHS"
  )

  if(EXISTS "${_SICNU_TEST_EXECUTABLE}")
    set(_probe_env "")
    if(_SICNU_TEST_DL_PATHS)
      if(WIN32)
        set(_dl_var PATH)
      elseif(APPLE)
        set(_dl_var DYLD_LIBRARY_PATH)
      else()
        set(_dl_var LD_LIBRARY_PATH)
      endif()
      set(_saved_dl "$ENV{${_dl_var}}")
      cmake_path(CONVERT "$ENV{${_dl_var}}" TO_NATIVE_PATH_LIST _env_dl)
      list(PREPEND _env_dl ${_SICNU_TEST_DL_PATHS})
      cmake_path(CONVERT "${_env_dl}" TO_NATIVE_PATH_LIST _paths)
      set(ENV{${_dl_var}} "${_paths}")
    endif()
    execute_process(
      COMMAND ${_SICNU_TEST_EXECUTOR} "${_SICNU_TEST_EXECUTABLE}" ${_SICNU_TEST_SPEC}
              --list-tests --verbosity quiet
      OUTPUT_VARIABLE _probe_out
      ERROR_VARIABLE _probe_err
      RESULT_VARIABLE _probe_rc
      WORKING_DIRECTORY "${_SICNU_TEST_WORKING_DIR}"
    )
    if(_SICNU_TEST_DL_PATHS)
      set(ENV{${_dl_var}} "${_saved_dl}")
    endif()

    if(NOT _probe_rc EQUAL 0)
      get_filename_component(_exe_name "${_SICNU_TEST_EXECUTABLE}" NAME_WE)
      set(_placeholder "${_SICNU_TEST_PREFIX}${_exe_name}_DISCOVERY_FAILED${_SICNU_TEST_SUFFIX}")
      message(WARNING
        "Catch2 test discovery failed for '${_SICNU_TEST_EXECUTABLE}' "
        "(result: ${_probe_rc}); registering '${_placeholder}' instead of "
        "aborting the CTest run.\n${_probe_err}")
      # The ctest file for this binary: CTEST_FILE may be passed twice by
      # Catch2 (the second one empty); the first non-empty value is the file.
      set(_ctest_file "")
      set(_next_is_file FALSE)
      foreach(_arg IN LISTS ARGN)
        if(_next_is_file)
          if(NOT _ctest_file AND NOT "${_arg}" STREQUAL "")
            set(_ctest_file "${_arg}")
          endif()
          set(_next_is_file FALSE)
        elseif("${_arg}" STREQUAL "CTEST_FILE")
          set(_next_is_file TRUE)
        endif()
      endforeach()
      if(NOT _ctest_file)
        message(FATAL_ERROR "SicnuCatchAddTests: no CTEST_FILE for ${_SICNU_TEST_EXECUTABLE}")
      endif()
      set(_placeholder_script
        "add_test([==[${_placeholder}]==] [==[${_SICNU_TEST_EXECUTABLE}]==] --list-tests)\n"
        "set_tests_properties([==[${_placeholder}]==] PROPERTIES WORKING_DIRECTORY "
        "[==[${_SICNU_TEST_WORKING_DIR}]==])\n")
      foreach(_p IN LISTS _SICNU_TEST_DL_PATHS)
        cmake_path(NATIVE_PATH _p _native)
        string(APPEND _placeholder_script
          "set_tests_properties([==[${_placeholder}]==] PROPERTIES ENVIRONMENT_MODIFICATION "
          "[==[${_dl_var}=path_list_prepend:${_native}]==])\n")
      endforeach()
      file(WRITE "${_ctest_file}" ${_placeholder_script})
      return()
    endif()
  endif()

  _catch_discover_tests_impl(${ARGV})
endfunction()
cmake_policy(POP)
