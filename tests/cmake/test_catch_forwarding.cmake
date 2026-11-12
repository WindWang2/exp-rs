cmake_minimum_required(VERSION 3.20)
file(MAKE_DIRECTORY "${TEST_DIR}")
set(SICNU_CATCH_ADD_TESTS_UPSTREAM "${TEST_DIR}/upstream.cmake")
file(WRITE "${SICNU_CATCH_ADD_TESTS_UPSTREAM}" [====[
function(catch_discover_tests_impl)
  if(NOT ARGC EQUAL 8)
    message(FATAL_ERROR "Argument count changed: ${ARGC}")
  endif()
  if(NOT "${ARGV0}" STREQUAL "TEST_EXECUTABLE" OR
     NOT "${ARGV1}" STREQUAL "/nonexistent/forwarding-probe" OR
     NOT "${ARGV2}" STREQUAL "TEST_SUFFIX" OR
     NOT "${ARGV3}" STREQUAL "" OR
     NOT "${ARGV4}" STREQUAL "TEST_PREFIX" OR
     NOT "${ARGV5}" STREQUAL "prefix;with;semicolons" OR
     NOT "${ARGV6}" STREQUAL "TEST_SPEC" OR
     NOT "${ARGV7}" STREQUAL "a]=]b]==]c")
    message(FATAL_ERROR "Forwarding changed an argument")
  endif()
endfunction()
]====])
configure_file("${WRAPPER}" "${TEST_DIR}/wrapper.cmake" @ONLY)
include("${TEST_DIR}/wrapper.cmake")
catch_discover_tests_impl(
  TEST_EXECUTABLE "/nonexistent/forwarding-probe"
  TEST_SUFFIX ""
  TEST_PREFIX "prefix;with;semicolons"
  TEST_SPEC "a]=]b]==]c")
