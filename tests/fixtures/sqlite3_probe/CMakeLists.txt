cmake_minimum_required(VERSION 3.20)
project(sqlite3_probe LANGUAGES NONE)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake")
include(SicnuDepDoctor)
message(STATUS "PROBE: mode=${PROBE_MODE}")
if(PROBE_MODE STREQUAL "direct")
  find_package(SQLite3 REQUIRED)
  message(STATUS "PROBE direct OK")
else()
  sicnu_require_dependency(SQLite3)
  # SECOND discovery in the same scope. This is what makes the case potent:
  # repo find modules (cmake/FindSqlite3.cmake) enforce "run once" through
  # directory-scope variables, so a FUNCTION-scoped wrapper - whose variables
  # die on return - would make this second call hit the module's guard and
  # FATAL ("SQLite::SQLite3 target should not have been defined at this point").
  # A macro (the shipped implementation) keeps the variables alive and the
  # second call short-circuits cleanly. vcpkg's find_package override re-enters
  # discovery the same way, which is how the regression was originally found.
  # Mirror what the vcpkg toolchain's find_package override does around every
  # call: it backs up and restores <Pkg>_FOUND, so the next discovery re-runs
  # the find module while the imported target from the first run persists.
  # That is exactly the state that trips cmake/FindSqlite3.cmake's run-once
  # guard ("SQLite::SQLite3 target should not have been defined at this point")
  # when the wrapper is a function, whose variables die on return - and passes
  # when it is a macro, whose variables stay in the caller's scope. Without
  # this reset CMake's cached <Pkg>_FOUND short-circuits the second call and
  # the case would pass either way (verified with knock-out experiments).
  # Only the CACHE entry is cleared: the plain variable is what the vcpkg
  # override leaves behind in the top-level scope, and it is exactly the state
  # a macro keeps and a function loses.
  unset(SQLITE3_FOUND CACHE)
  find_package(SQLite3 REQUIRED)
  message(STATUS "PROBE wrapped OK")
endif()
