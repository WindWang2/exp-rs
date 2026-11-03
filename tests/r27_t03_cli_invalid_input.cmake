# tests/r27_t03_cli_invalid_input.cmake — #1798 (SPP-6) oracle O13
#
# The legacy `--pipeline` path must answer malformed pipeline documents
# with the published InvalidInput(6) exit code — never std::terminate
# (134), never a silent generic 1. Runs the REAL CLI binary against four
# shape-invalid documents (script-mode test; fixtures written to a temp
# dir, cleaned up on exit).
cmake_minimum_required(VERSION 3.20)

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/r27_t03_cli_fixtures")
set(FIX "${CMAKE_CURRENT_BINARY_DIR}/r27_t03_cli_fixtures")
file(WRITE "${FIX}/root_array.json" "[1]")
file(WRITE "${FIX}/step_int.json" "{\"steps\":[1]}")
file(WRITE "${FIX}/operator_obj.json" "{\"steps\":[{\"operator\":{}}]}")
file(WRITE "${FIX}/garbage.json" "not json at all")

set(failures 0)
foreach(name root_array step_int operator_obj garbage)
  execute_process(
    COMMAND "${CLI}" --pipeline "${FIX}/${name}.json"
    OUTPUT_QUIET ERROR_QUIET
    RESULT_VARIABLE code)
  if(NOT code EQUAL 6)
    message(SEND_ERROR "r27_t03_cli_invalid_input: ${name}.json exited ${code}, expected 6 (InvalidInput)")
    set(failures 1)
  else()
    message(STATUS "r27_t03: ${name}.json -> exit 6 OK")
  endif()
endforeach()

if(failures EQUAL 0)
  message(STATUS "r27_t03_cli_invalid_input: all four shapes exit 6")
endif()
