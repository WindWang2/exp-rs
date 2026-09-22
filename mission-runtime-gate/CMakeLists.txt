# Mission Runtime 13.0 gate harness (out-of-tree, local verification only)
#
# Why out-of-tree: the in-repo `sicnu_geo_rs` build compiles the vendored QGIS
# subset from src/core, which is out of proportion in this environment (see
# .planning/glm53-mission-runtime-13/DECISIONS.md D1). The mission runtime is
# deliberately QGIS-free, so this harness compiles the REAL repo sources with
# Qt6 Core+Xml (+Gui/Widgets for the paged model) + jsoncpp and executes the
# gates here. The same sources are registered in tests/CMakeLists.txt for the
# real build.
#
# The three QGIS-dependent symbols the mission context chain references
# (objectKindToken, ContextRules::primaryObject, ContextRules::selectedLayerIds)
# are provided by stubs/ with a source-parity gate asserting the stubs match
# the real implementations (tests/test_mission_runtime_parity.cpp).

cmake_minimum_required(VERSION 3.20)
project(mission_runtime_gate LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_AUTOMOC ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ── Catch2 (local source, no network) ────────────────────────────────────
include(FetchContent)
set(FETCHCONTENT_SOURCE_DIR_CATCH2 "${MISSION_GATE_CATCH2_SRC}" CACHE PATH "local Catch2 v3 source")
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.7.1
)
FetchContent_MakeAvailable(Catch2)
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
include(CTest)
enable_testing()

# ── Qt6 ──────────────────────────────────────────────────────────────────
find_package(Qt6 REQUIRED COMPONENTS Core Xml Gui Widgets)

# ── jsoncpp (system) ─────────────────────────────────────────────────────
find_library(JSONCPP_LIBRARY NAMES jsoncpp REQUIRED)
find_path(JSONCPP_INCLUDE_DIR json/json.h REQUIRED)

# Normalised (no ".." components): the parity gate opens these files at
# runtime and some sandboxes reject absolute paths containing "..".
file(REAL_PATH "${CMAKE_CURRENT_LIST_DIR}/.." SICNU_SOURCE_DIR)
set(SICNU_SOURCE_DIR "${SICNU_SOURCE_DIR}" CACHE PATH "repo root" FORCE)

set(SICNU_MISSION_SRC_ROOT "${SICNU_SOURCE_DIR}")
include("${SICNU_SOURCE_DIR}/cmake/SicnuMissionRuntimeSources.cmake")
set(MISSION_GATE_SOURCES
  ${SICNU_MISSION_RUNTIME_SOURCES_GATE}
  ${CMAKE_CURRENT_LIST_DIR}/stubs/mission_gate_stubs.cpp
)

function(mission_gate_add_test NAME)
  add_executable(${NAME} ${CMAKE_CURRENT_LIST_DIR}/tests/${NAME}.cpp ${MISSION_GATE_SOURCES})
  target_link_libraries(${NAME} PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    Qt6::Xml
    Qt6::Gui
    Qt6::Widgets
    ${JSONCPP_LIBRARY}
  )
  target_include_directories(${NAME} PRIVATE
    ${SICNU_SOURCE_DIR}/src
    ${SICNU_SOURCE_DIR}/src/agent
    ${JSONCPP_INCLUDE_DIR}
  )
  # The parity gate parses the real MCP dispatch / provider / Pi bridge from
  # the source tree, so a forgotten surface wiring fails the test.
  target_compile_definitions(${NAME} PRIVATE
    SICNU_MISSION_SOURCE_DIR="${SICNU_SOURCE_DIR}"
  )
  set_target_properties(${NAME} PROPERTIES AUTOMOC ON)
  catch_discover_tests(${NAME} DISCOVERY_MODE PRE_TEST)
endfunction()

mission_gate_add_test(test_mission_runtime_persistence)
mission_gate_add_test(test_mission_tools)
mission_gate_add_test(test_mission_runtime_parity)
mission_gate_add_test(test_mission_runtime_scale)
