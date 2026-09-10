# SICNU GEO RS — Unit Tests (Catch2)
include(Catch)

if(WIN32 AND TARGET Catch2WithMain)
  set_property(TARGET Catch2WithMain PROPERTY SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/sicnu_test_main.cpp")
  # sicnu_test_main wires the unified-trace env bootstrap (Verification 7.0),
  # which references sicnu_runtime. Link (not just include) here so EVERY
  # Catch2 consumer inherits the dependency via LINK_ONLY — io test targets
  # and light fuzz targets link only Catch2::Catch2WithMain.
  target_include_directories(Catch2WithMain PRIVATE ${CMAKE_SOURCE_DIR}/src)
  target_link_libraries(Catch2WithMain PRIVATE sicnu_runtime)
endif()

# Wrapper around catch_discover_tests that injects LSAN_OPTIONS when sanitizers
# are enabled, suppressing known singleton leaks (QgsExpression functions,
# QgsNumericFormatRegistry, Nvidia EGL driver). Callers may pass extra
# test PROPERTIES (e.g. RUN_SERIAL TRUE) after the NAME.
#
# DL_PATHS prepends /usr/lib for both PRE_TEST discovery and the tests
# themselves (#730): Catch discovery execute_process does not inherit
# per-test ENVIRONMENT_MODIFICATION, and conda RUNPATH otherwise shadows
# system libxml2. CTestCustom.cmake (sicnu_configure_test_harness_env)
# still pins PYTHONHOME / QT_IM_MODULE for the ctest process.
function(sicnu_discover_tests NAME)
  set(_discovery_props "")
  if(ENABLE_SANITIZERS)
    list(APPEND _discovery_props "ENVIRONMENT_MODIFICATION" "LSAN_OPTIONS=set:detect_leaks=0")
  endif()
  if(ARGN)
    # ARGN is a flat list of key/value test properties (RUN_SERIAL TRUE, ...).
    list(APPEND _discovery_props ${ARGN})
  endif()
  set(_dl_paths "")
  if(SICNU_TEST_DL_PATHS)
    set(_dl_paths DL_PATHS ${SICNU_TEST_DL_PATHS})
  endif()
  # PRE_TEST avoids race conditions in parallel builds where discovery
  # tries to run the test executable before it is linked.
  catch_discover_tests(${NAME}
    DISCOVERY_MODE PRE_TEST
    ${_dl_paths}
    PROPERTIES ${_discovery_props})
endfunction()

# Helper function to add a test executable. Extra args (TIMEOUT n,
# RUN_SERIAL TRUE, ...) are forwarded to sicnu_discover_tests as test
# properties — previously they were accepted and silently dropped.
function(sicnu_add_test NAME)
  cmake_parse_arguments(SAT "" "" "" ${ARGN})
  qt_add_executable(${NAME} ${NAME}.cpp)
  target_link_libraries(${NAME} PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    qgis_core
    qgis_gui
    sicnu_data
    sicnu_processing
    sicnu_jobs
    sicnu_task_center
    sicnu_agent
    sicnu_operators
    # agent_workflow_executor (inside sicnu_task_center) routes through
    # WorkflowRunCoordinator; its symbols resolve at final link time here.
    sicnu_workflow_runtime
  )
  target_include_directories(${NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/stubs
    ${CMAKE_BINARY_DIR}
  )
  target_compile_definitions(${NAME} PRIVATE
    CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
  )
  if(SAT_UNPARSED_ARGUMENTS)
    sicnu_discover_tests(${NAME} ${SAT_UNPARSED_ARGUMENTS})
  else()
    sicnu_discover_tests(${NAME})
  endif()
endfunction()

# Geospatial I/O Foundation 4.0: light test executables. Links only
# Catch2 + sicnu_geospatial (GDAL + jsoncpp transitively) — no Qt, no QGIS —
# so the I/O contract suites build and run without the full vendored chain.
# Fixtures are synthesized at runtime by the suites (repo convention).
function(sicnu_add_io_test NAME)
  add_executable(${NAME} ${NAME}.cpp)
  target_link_libraries(${NAME} PRIVATE
    Catch2::Catch2WithMain
    Sicnu::Geospatial
  )
  target_include_directories(${NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
  )
  target_compile_definitions(${NAME} PRIVATE
    CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
  )
  # Output next to the vcpkg-deployed runtime DLLs so the executables start
  # without per-exe DLL copies (gdal.dll & friends land in the build root).
  set_target_properties(${NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  sicnu_discover_tests(${NAME})
endfunction()

# Isolation runtime 5.0: light SDK test executables. Links only Catch2 +
# sicnu_sdk (jsoncpp transitively) — no Qt, no QGIS — so the IPC/capability
# contract suites build and run fast on every platform lane.
function(sicnu_add_sdk_test NAME)
  add_executable(${NAME} ${NAME}.cpp)
  target_link_libraries(${NAME} PRIVATE
    Catch2::Catch2WithMain
    sicnu_sdk
  )
  target_include_directories(${NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/sdk
    ${CMAKE_BINARY_DIR}
  )
  target_compile_definitions(${NAME} PRIVATE
    CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
  )
  set_target_properties(${NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  sicnu_discover_tests(${NAME})
endfunction()

# Helper function to add a Catch2 offscreen UI dialog test executable
function(sicnu_add_dialog_test NAME)
  cmake_parse_arguments(PARSE_ARGV 1 ARG "NO_DEFAULT_MAIN" "MAIN_SOURCE" "SOURCES;EXTRA_LIBS;EXTRA_INCLUDES")

  set(_srcs ${NAME}.cpp)
  if(ARG_MAIN_SOURCE)
    list(APPEND _srcs ${ARG_MAIN_SOURCE})
  endif()
  if(ARG_SOURCES)
    list(APPEND _srcs ${ARG_SOURCES})
  endif()
  if(ARG_UNPARSED_ARGUMENTS)
    list(APPEND _srcs ${ARG_UNPARSED_ARGUMENTS})
  endif()

  add_executable(${NAME}
    ${_srcs}
    ${CMAKE_SOURCE_DIR}/src/app/widgets/raster_layer_combo.cpp
    ${CMAKE_SOURCE_DIR}/src/app/dialogs/raster_processing_dialog_base.cpp
    ${CMAKE_SOURCE_DIR}/src/app/shell/processing_job_adapter.cpp
    ${CMAKE_SOURCE_DIR}/src/app/shell/gui_job_adapter.cpp
    ${CMAKE_SOURCE_DIR}/src/app/dialogs/dialog_help_catalog.cpp
    ${CMAKE_SOURCE_DIR}/src/app/dialogs/dialog_utils.cpp
    ${CMAKE_SOURCE_DIR}/src/app/widgets/rs_scan_pool.cpp
  )
  set_target_properties(${NAME} PROPERTIES AUTOMOC ON)

  set(_catch_lib Catch2::Catch2WithMain)
  if(ARG_NO_DEFAULT_MAIN OR ARG_MAIN_SOURCE)
    set(_catch_lib Catch2::Catch2)
  endif()

  target_link_libraries(${NAME} PRIVATE
    ${_catch_lib}
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Test
    Qt6::Concurrent
    sicnu_jobs
    sicnu_operators
    sicnu_task_center
    qgis_core
    qgis_gui
    sicnu_processing
    sicnu_data
    ${ARG_EXTRA_LIBS}
  )
  target_include_directories(${NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/app
    ${CMAKE_BINARY_DIR}
    ${ARG_EXTRA_INCLUDES}
  )
  target_compile_definitions(${NAME} PRIVATE
    CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
  )
  sicnu_link_jsoncpp(${NAME})
  sicnu_discover_tests(${NAME})
endfunction()

# Single jsoncpp resolution for hand-rolled test targets (#653): the canonical
# if(TARGET jsoncpp)/elseif(JSONCPP_LIBRARIES)/else fallback that used to be
# copy-pasted per target. Targets with DIFFERENT fallback policies (no-else,
# if-only, pkg-config probes) deliberately keep their own blocks.
function(sicnu_link_jsoncpp tgt)
    if(TARGET jsoncpp)
        target_link_libraries(${tgt} PRIVATE jsoncpp)
    elseif(JSONCPP_LIBRARIES)
        target_include_directories(${tgt} PRIVATE ${JSONCPP_INCLUDE_DIRS})
        target_link_libraries(${tgt} PRIVATE ${JSONCPP_LIBRARIES})
    else()
        target_link_libraries(${tgt} PRIVATE jsoncpp)
    endif()
endfunction()

# Core and domain tests
sicnu_add_test(test_crs)

# Geospatial I/O Foundation 4.0 (light targets — see sicnu_add_io_test above).
sicnu_add_io_test(test_io_canonical_metadata)
sicnu_add_io_test(test_io_crs_policy)
sicnu_add_io_test(test_io_raster_contract)
sicnu_add_io_test(test_io_vector_contract)
sicnu_add_io_test(test_io_roundtrip_matrix)
sicnu_add_io_test(test_io_cog)
sicnu_add_io_test(test_io_stac)
sicnu_add_io_test(test_io_multidim)
find_library(NETCDF_LIBRARY NAMES netcdf)
target_link_libraries(test_io_multidim PRIVATE ${NETCDF_LIBRARY})
target_link_libraries(test_io_roundtrip_matrix PRIVATE ${NETCDF_LIBRARY})
sicnu_add_io_test(test_io_products)
sicnu_add_io_test(test_io_doctor)
sicnu_add_io_test(test_io_atomic_failures)
sicnu_add_io_test(test_io_fidelity)
sicnu_add_io_test(test_io_paths)
sicnu_add_io_test(test_io_uri)
sicnu_add_io_test(test_io_product_registry)
sicnu_add_io_test(test_io_probe)
sicnu_add_io_test(test_adversarial_m3)

# Remote range harness (ADR 0139): local HTTP fixture with byte accounting.
sicnu_add_io_test(test_io_remote_range)
target_sources(test_io_remote_range PRIVATE support/http_range_server.cpp)
if(WIN32)
  target_link_libraries(test_io_remote_range PRIVATE ws2_32)
endif()

# 7.0 remote source identity & validators (shared HTTP fixture).
sicnu_add_io_test(test_io_remote_validator)
target_sources(test_io_remote_validator PRIVATE support/http_range_server.cpp)
if(WIN32)
  target_link_libraries(test_io_remote_validator PRIVATE ws2_32)
endif()

# 7.0 bounded byte-range cache over remote rasters.
sicnu_add_io_test(test_io_range_cache)
target_sources(test_io_range_cache PRIVATE support/http_range_server.cpp)
if(WIN32)
  target_link_libraries(test_io_range_cache PRIVATE ws2_32)
endif()

# 7.0 STAC API client (loopback JSON fixture).
sicnu_add_io_test(test_io_stac_client)
target_sources(test_io_stac_client PRIVATE support/http_stac_server.cpp support/http_range_server.cpp)
if(WIN32)
  target_link_libraries(test_io_stac_client PRIVATE ws2_32)
endif()

# 7.0 modern vector interop audit (field types / null / collections / CRS /
# streaming / driver-gated GeoParquet).
sicnu_add_io_test(test_io_vector_interop)

# 7.0 generalized grid classification & placement (rotated/flipped/GCP/RPC).
sicnu_add_io_test(test_io_grid_descriptor)

# io:* operator family suite — rides the operator chain (Qt + sicnu_operators).
sicnu_add_io_test(test_io_operators)
target_link_libraries(test_io_operators PRIVATE Qt6::Core sicnu_operators)
sicnu_link_jsoncpp(test_io_operators)

# Geospatial I/O benchmark harness (Phase 15): run manually —
#   benchmark_io --out benchmarks/io-foundation-4.json
add_executable(benchmark_io benchmark_io.cpp)
target_link_libraries(benchmark_io PRIVATE Sicnu::Geospatial)
target_include_directories(benchmark_io PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_BINARY_DIR})
target_compile_definitions(benchmark_io PRIVATE CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
set_target_properties(benchmark_io PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  QT_QML_MODULE_NO_IMPORT_SCAN TRUE
)

sicnu_add_test(test_geometry)
sicnu_add_test(test_layers)
sicnu_add_test(test_algorithm_engine)
sicnu_add_test(test_task_center)
sicnu_add_test(test_atomic_algorithm_adapter)
sicnu_add_test(test_atomic_algorithm_registry)
sicnu_add_test(test_descriptor_outputs)
sicnu_add_test(test_schema_validator)
sicnu_add_test(test_preflight)
sicnu_add_test(test_agent_golden_workflow)
sicnu_add_test(test_atomic_registry_contract)
sicnu_add_test(test_catalog_size)
sicnu_add_test(test_provider_algorithm_adapter)
sicnu_add_test(test_agent_workflow_executor)
sicnu_add_test(test_codeeditor_stubs)
sicnu_add_test(test_vector_overlay_crs_transform)

add_executable(test_interaction_tools test_interaction_tools.cpp ../src/agent/mcp_server.cpp ../src/app/shell/processing_job_adapter.cpp)
target_link_libraries(test_interaction_tools PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_data
  sicnu_processing
  sicnu_jobs
  sicnu_task_center
  sicnu_agent
  sicnu_operators
  GDAL::GDAL
)
target_include_directories(test_interaction_tools PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_interaction_tools)

add_executable(test_raster_display_tools test_raster_display_tools.cpp ../src/agent/mcp_server.cpp ../src/app/shell/processing_job_adapter.cpp)
target_link_libraries(test_raster_display_tools PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_data
  sicnu_processing
  sicnu_jobs
  sicnu_task_center
  sicnu_agent
  sicnu_operators
  GDAL::GDAL
)
target_include_directories(test_raster_display_tools PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_raster_display_tools)

# ToolCallDispatcher (ADR 0021) — headless; fake sink/watcher, real registry.
# Deliberately avoids the full sicnu_add_test link set: the dispatcher module
# is GUI-free (QtCore + jsoncpp only).
add_executable(test_tool_call_dispatcher test_tool_call_dispatcher.cpp)
target_link_libraries(test_tool_call_dispatcher PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  sicnu_processing
  sicnu_task_center
  sicnu_workflow_runtime
  sicnu_operators
)
sicnu_link_jsoncpp(test_tool_call_dispatcher)
target_include_directories(test_tool_call_dispatcher PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_tool_call_dispatcher)

# Unified Execution Plane — the real spine (plane → TaskCenter → JobEngine →
# registry adapter) with behavioral stub algorithms; covers the #559 deadlock
# regression, exactly-once completion, cancel/timeout lifecycles, resource
# admission and transactional output commit. Needs sicnu_data for the
# DataManager/OutputCommitter path; QtCore-only otherwise.
add_executable(test_execution_plane test_execution_plane.cpp)
target_link_libraries(test_execution_plane PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Test
  qgis_core
  Sicnu::data
  sicnu_processing
  sicnu_task_center
  sicnu_operators
)
sicnu_link_jsoncpp(test_execution_plane)
target_include_directories(test_execution_plane PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_execution_plane)

# Performance & Benchmark test for Image Fusion (headless benchmark target)
add_executable(test_perf_fusion test_perf_fusion.cpp)
target_link_libraries(test_perf_fusion PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  sicnu_processing
  Sicnu::data
)
target_include_directories(test_perf_fusion PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/core
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_perf_fusion)


# WorkspaceSnapshot capture needs ActiveViewHost translation unit + display types.
qt_add_executable(test_workspace_snapshot
  test_workspace_snapshot.cpp
  ${CMAKE_SOURCE_DIR}/src/app/active_view_host.cpp
)
target_link_libraries(test_workspace_snapshot PRIVATE
  Catch2::Catch2
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_data
  sicnu_agent
  Sicnu::qgis_display
)
target_include_directories(test_workspace_snapshot PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_workspace_snapshot)

# Workspace Understanding 3.0 — stable entity ids + WorkspaceState contract.
qt_add_executable(test_workspace_state
  test_workspace_state.cpp
)
target_link_libraries(test_workspace_state PRIVATE
  Catch2::Catch2
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  sicnu_data
  sicnu_agent
)
if(TARGET jsoncpp)
  target_link_libraries(test_workspace_state PRIVATE jsoncpp)
endif()
target_include_directories(test_workspace_state PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_workspace_state)

# Spatial Scientist 3.0 tool-level tests (capability/workflow/symbology/commands).
qt_add_executable(test_agent_tools_3
  test_agent_tools_3.cpp
)
target_link_libraries(test_agent_tools_3 PRIVATE
  Catch2::Catch2
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_data
  sicnu_agent
  sicnu_operators
  sicnu_workflow
  sicnu_processing
  sicnu_task_center
)
if(TARGET jsoncpp)
  target_link_libraries(test_agent_tools_3 PRIVATE jsoncpp)
endif()
target_include_directories(test_agent_tools_3 PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_agent_tools_3)

# MapSpec + cartography libraries (compiler roundtrip, registries, charts,
# preflight/repair loop).
qt_add_executable(test_mapspec
  test_mapspec.cpp
  test_cartography_tokens.cpp
  test_cartography_library.cpp
  test_cartography_quality.cpp
  test_cartography_templates.cpp
  test_cartography_visual.cpp
  test_platform5.cpp
  test_platform6.cpp
  test_platform6_knowledge.cpp
  test_knowledge_drift.cpp
  test_platform7.cpp
  test_platform8.cpp
)
target_compile_definitions(test_mapspec PRIVATE
  SICNU_CARTOGRAPHY_DATA_DIR="${CMAKE_SOURCE_DIR}/data/cartography"
  CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
target_link_libraries(test_mapspec PRIVATE
  Catch2::Catch2
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_data
  sicnu_agent
  sicnu_operators
  sicnu_workflow
  sicnu_processing
  sicnu_task_center
)
if(TARGET jsoncpp)
  target_link_libraries(test_mapspec PRIVATE jsoncpp)
endif()
target_include_directories(test_mapspec PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_mapspec)

# #35 — UI callers must not bypass Task Center; task views are projections.
add_executable(test_ui_task_center_contract
  test_ui_task_center_contract.cpp
  # AsyncAlgorithmRunner's implementation is compiled directly into the test
  # (a deprecated legacy type not built into any library). async_runner_base.cpp is
  # the out-of-line TU that forces the base class's moc (signals) into the link.
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/async_algorithm_runner.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/async_runner_base.cpp
)
target_link_libraries(test_ui_task_center_contract PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Widgets
  qgis_core
  sicnu_jobs
  sicnu_task_center
)
target_include_directories(test_ui_task_center_contract PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
target_compile_definitions(test_ui_task_center_contract PRIVATE
  CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
sicnu_discover_tests(test_ui_task_center_contract)

add_executable(test_async_algorithm_runner
  test_async_algorithm_runner.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/async_algorithm_runner.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/async_runner_base.cpp
)
target_link_libraries(test_async_algorithm_runner PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Widgets
  Qt6::Test
  qgis_core
  sicnu_jobs
  sicnu_task_center
)
target_include_directories(test_async_algorithm_runner PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_async_algorithm_runner)

add_executable(test_theme_selector_parity
  test_theme_selector_parity.cpp
)
target_link_libraries(test_theme_selector_parity PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
)
target_include_directories(test_theme_selector_parity PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
target_compile_definitions(test_theme_selector_parity PRIVATE
  CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
sicnu_discover_tests(test_theme_selector_parity)

# ── Workbench 5.0 contracts (workspace model / selection context / commands) ──
add_executable(test_workbench_host
  test_workbench_host.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/workbench_host.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/adapters.cpp
)
target_link_libraries(test_workbench_host PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
)
target_include_directories(test_workbench_host PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
target_compile_definitions(test_workbench_host PRIVATE
  CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
sicnu_discover_tests(test_workbench_host)

# ── Workbench 7.0 contracts (shutdown/switch policy over bench lifecycles) ──
add_executable(test_workbench_shutdown_policy
  test_workbench_shutdown_policy.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/workbench_host.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/shutdown_policy.cpp
)
target_link_libraries(test_workbench_shutdown_policy PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
)
target_include_directories(test_workbench_shutdown_policy PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_workbench_shutdown_policy)

# ── Workbench 7.0: provenance inspector section over DataManager + WorkspaceService ──
add_executable(test_provenance_section
  test_provenance_section.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/inspector_host.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/provenance_section.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/selection_context.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/workbench_host.cpp
)
target_link_libraries(test_provenance_section PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_data
)
target_include_directories(test_provenance_section PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_provenance_section)

# ── Workbench 7.0: unified processing history projection (TaskCenter + runs) ──
add_executable(test_processing_history_model
  test_processing_history_model.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/processing_history_model.cpp
)
target_link_libraries(test_processing_history_model PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  sicnu_task_center
  sicnu_workflow
)
target_include_directories(test_processing_history_model PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_processing_history_model)

# ── Workbench 7.0: temporal scene browser model (paged, filtered) ──
add_executable(test_temporal_scene_model
  test_temporal_scene_model.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/temporal_scene_model.cpp
)
target_link_libraries(test_temporal_scene_model PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  sicnu_processing
)
target_include_directories(test_temporal_scene_model PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_temporal_scene_model)

add_executable(test_selection_context
  test_selection_context.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/selection_context.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/workbench_host.cpp
)
target_link_libraries(test_selection_context PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
  qgis_core
  qgis_gui
)
target_include_directories(test_selection_context PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
target_compile_definitions(test_selection_context PRIVATE
  CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
sicnu_discover_tests(test_selection_context)

add_executable(test_command_registry
  test_command_registry.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/command_registry.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/selection_context.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/workbench_host.cpp
)
target_link_libraries(test_command_registry PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
)
target_include_directories(test_command_registry PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
target_compile_definitions(test_command_registry PRIVATE
  CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
sicnu_discover_tests(test_command_registry)

add_executable(test_command_palette
  test_command_palette.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/command_registry.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/command_palette.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/selection_context.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/workbench_host.cpp
)
target_link_libraries(test_command_palette PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
  qgis_core
  qgis_gui
)
target_include_directories(test_command_palette PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
target_compile_definitions(test_command_palette PRIVATE
  CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
sicnu_discover_tests(test_command_palette)

add_executable(test_inspector_host
  test_inspector_host.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/inspector_host.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/selection_context.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/workbench_host.cpp
)
target_link_libraries(test_inspector_host PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
)
target_include_directories(test_inspector_host PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
target_compile_definitions(test_inspector_host PRIVATE
  CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
sicnu_discover_tests(test_inspector_host)

add_executable(test_adversarial_m4
  test_adversarial_m4.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/inspector_host.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/selection_context.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/workbench_host.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/command_registry.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/adapters.cpp
)
target_link_libraries(test_adversarial_m4 PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  Sicnu::qgis_display
  sicnu_data
)
target_include_directories(test_adversarial_m4 PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
target_compile_definitions(test_adversarial_m4 PRIVATE
  CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
sicnu_discover_tests(test_adversarial_m4)

add_executable(test_adversarial_m5
  test_adversarial_m5.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/histogram_widget.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/roi_statistics_widget.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/rs_scan_pool.cpp
  ${CMAKE_SOURCE_DIR}/src/processing/gdal/gdal_dataset_wrapper.cpp
)
target_link_libraries(test_adversarial_m5 PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_data
  sicnu_processing
  sicnu_jobs
  sicnu_task_center
  sicnu_operators
  GDAL::GDAL
)
target_include_directories(test_adversarial_m5 PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_SOURCE_DIR}/src/stubs
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_adversarial_m5 PROPERTIES AUTOMOC ON)
target_compile_definitions(test_adversarial_m5 PRIVATE
  CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
sicnu_discover_tests(test_adversarial_m5)

add_executable(test_adversarial_m6
  test_adversarial_m6.cpp
)
target_link_libraries(test_adversarial_m6 PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_data
  sicnu_agent
  sicnu_operators
  sicnu_workflow
  sicnu_processing
  sicnu_task_center
)
if(TARGET jsoncpp)
  target_link_libraries(test_adversarial_m6 PRIVATE jsoncpp)
endif()
target_include_directories(test_adversarial_m6 PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
target_compile_definitions(test_adversarial_m6 PRIVATE
  CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
sicnu_discover_tests(test_adversarial_m6)

add_executable(test_interactive_session_contract
  test_interactive_session_contract.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/session_adapters.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/interactive_session.cpp
  ${CMAKE_SOURCE_DIR}/src/app/shell/gui_job_adapter.cpp
)
target_link_libraries(test_interactive_session_contract PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_data
  sicnu_processing
  sicnu_jobs
  sicnu_task_center
  sicnu_agent
  sicnu_operators
  sicnu_workflow_runtime
)
target_include_directories(test_interactive_session_contract PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app/classification
  ${CMAKE_BINARY_DIR}
)
target_compile_definitions(test_interactive_session_contract PRIVATE
  CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
if(TARGET qgis_app_classify)
  target_link_libraries(test_interactive_session_contract PRIVATE qgis_app_classify)
else()
  # The adapter wraps the classification lab window; without the lab lib the
  # adapter body cannot link. The contract still compiles interface-side.
  set_source_files_properties(${CMAKE_SOURCE_DIR}/src/app/workbench/session_adapters.cpp
    PROPERTIES HEADER_FILE_ONLY ON)
endif()
sicnu_discover_tests(test_interactive_session_contract)

qt_add_executable(test_data_manager
  test_data_manager.cpp
)
target_link_libraries(test_data_manager PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  sicnu_data
)
target_include_directories(test_data_manager PRIVATE
  ${CMAKE_SOURCE_DIR}/src
)
sicnu_discover_tests(test_data_manager)

# test_paths has custom main (needs QCoreApplication for AppPaths)
add_executable(test_paths test_paths.cpp)
target_link_libraries(test_paths PRIVATE
  Catch2::Catch2
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
)
target_include_directories(test_paths PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
target_compile_definitions(test_paths PRIVATE
  CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
sicnu_discover_tests(test_paths)

# Atmospheric correction tests
add_executable(test_atmospheric test_atmospheric.cpp)
target_link_libraries(test_atmospheric PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
  GDAL::GDAL
)
target_include_directories(test_atmospheric PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_atmospheric)

# Radiometric calibration tests
add_executable(test_radiometric_calibration test_radiometric_calibration.cpp)
target_link_libraries(test_radiometric_calibration PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Xml
  qgis_core
  qgis_gui
  sicnu_processing
  GDAL::GDAL
)
target_include_directories(test_radiometric_calibration PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_radiometric_calibration)

# Band math tests
add_executable(test_band_math test_band_math.cpp)
target_link_libraries(test_band_math PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
  GDAL::GDAL
)
target_include_directories(test_band_math PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_band_math)

# Spectral index tests
add_executable(test_spectral_indices test_spectral_indices.cpp)
target_link_libraries(test_spectral_indices PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
  GDAL::GDAL
)
target_include_directories(test_spectral_indices PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_spectral_indices)

# Shared NoData sentinel policy (Scientific Algorithms Foundation 4.0)
add_executable(test_nodata_utils test_nodata_utils.cpp)
target_link_libraries(test_nodata_utils PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  sicnu_processing
  GDAL::GDAL
)
target_include_directories(test_nodata_utils PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_nodata_utils)

# Shared scientific primitives (Scientific Algorithm Foundation 5.0, A):
# histogram/otsu/quantile, morphology, connected components, distance
# transform, exact quantiles, window contract.
add_executable(test_primitives5 test_primitives5.cpp)
target_link_libraries(test_primitives5 PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  sicnu_processing
  GDAL::GDAL
)
target_include_directories(test_primitives5 PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_primitives5)

# Topographic correction (Foundation 5.0 Milestone B1): kernel closed forms +
# synthetic-scene operator E2E + grid-refusal contract.
add_executable(test_topographic_correction test_topographic_correction.cpp)
target_link_libraries(test_topographic_correction PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  sicnu_processing
  sicnu_operators
  GDAL::GDAL
)
sicnu_link_jsoncpp(test_topographic_correction)
target_include_directories(test_topographic_correction PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_topographic_correction)

# Grid & resampling foundation (Foundation 6.0, Milestone D):
# rs:resample / rs:align known answers over the GDAL warp seam.
add_executable(test_grid_operators test_grid_operators.cpp)
target_link_libraries(test_grid_operators PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  sicnu_processing
  sicnu_operators
  GDAL::GDAL
)
sicnu_link_jsoncpp(test_grid_operators)
target_include_directories(test_grid_operators PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_grid_operators)

# Scientific contracts (Foundation 6.0): canonical numeric-domain decisions
# resolved once per raster + kernel regime consistency.
add_executable(test_scientific_contracts test_scientific_contracts.cpp)
target_link_libraries(test_scientific_contracts PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  sicnu_processing
  GDAL::GDAL
)
sicnu_link_jsoncpp(test_scientific_contracts)
target_include_directories(test_scientific_contracts PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_scientific_contracts)

# Spectral derivatives (Foundation 5.0 Milestone B3): kernel closed forms +
# operator E2E over an explicit wavelength axis + index-space refusal.
add_executable(test_spectral_derivative test_spectral_derivative.cpp)
target_link_libraries(test_spectral_derivative PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  sicnu_processing
  sicnu_operators
  GDAL::GDAL
)
sicnu_link_jsoncpp(test_spectral_derivative)
target_include_directories(test_spectral_derivative PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_spectral_derivative)

# Spectral target detection (Foundation 5.0 Milestone C): matched filter + ACE
# kernel closed forms, planted-target E2E, target-length contract.
add_executable(test_spectral_detection test_spectral_detection.cpp)
target_link_libraries(test_spectral_detection PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  sicnu_processing
  sicnu_operators
  GDAL::GDAL
)
sicnu_link_jsoncpp(test_spectral_detection)
target_include_directories(test_spectral_detection PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_spectral_detection)

# SAR foundation (Foundation 5.0 Milestone D): dual-pol features + terrain
# geometry closed forms, operator E2E, geometry refusals.
add_executable(test_sar_orbit test_sar_orbit.cpp)
target_link_libraries(test_sar_orbit PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  sicnu_processing
)
target_include_directories(test_sar_orbit PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_sar_orbit)

add_executable(test_sar_foundation5 test_sar_foundation5.cpp)
target_link_libraries(test_sar_foundation5 PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  sicnu_processing
  sicnu_operators
  GDAL::GDAL
)
sicnu_link_jsoncpp(test_sar_foundation5)
target_include_directories(test_sar_foundation5 PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_sar_foundation5)

# Terrain foundation (Foundation 5.0 Milestone F): ZT curvatures, MD
# hillshade, local relief, priority-flood fill, D8 routing + operator E2E.
add_executable(test_terrain_foundation5 test_terrain_foundation5.cpp)
target_link_libraries(test_terrain_foundation5 PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  sicnu_processing
  sicnu_operators
  GDAL::GDAL
)
sicnu_link_jsoncpp(test_terrain_foundation5)
target_include_directories(test_terrain_foundation5 PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_terrain_foundation5)

# Raster spatial operators (Foundation 5.0 Milestone G): morphology, labeling,
# sieve, fill holes, proximity, extrema, focal stats (operator E2E).
add_executable(test_raster_spatial5 test_raster_spatial5.cpp)
target_link_libraries(test_raster_spatial5 PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  sicnu_processing
  sicnu_operators
  GDAL::GDAL
)
sicnu_link_jsoncpp(test_raster_spatial5)
target_include_directories(test_raster_spatial5 PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_raster_spatial5)

# Spectral classification (SAM + Continuum Removal) kernel tests
add_executable(test_spectral_classification test_spectral_classification.cpp)
target_link_libraries(test_spectral_classification PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  sicnu_processing
  sicnu_operators
  GDAL::GDAL
  ${JSONCPP_LIBRARIES}
)
if(JSONCPP_CFLAGS_OTHER)
  target_compile_options(test_spectral_classification PRIVATE ${JSONCPP_CFLAGS_OTHER})
endif()
target_include_directories(test_spectral_classification PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/operators
  ${CMAKE_BINARY_DIR}
)
target_include_directories(test_spectral_classification PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_spectral_classification)

# MathUtils tests
add_executable(test_math_utils test_math_utils.cpp)
target_link_libraries(test_math_utils PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
  GDAL::GDAL
)
target_include_directories(test_math_utils PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_math_utils)

# GDAL utility tests
add_executable(test_gdal_utils test_gdal_utils.cpp)
target_link_libraries(test_gdal_utils PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
  GDAL::GDAL
)
target_include_directories(test_gdal_utils PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_gdal_utils)

# Raster NDVI algorithm tests
add_executable(test_raster_ndvi test_raster_ndvi.cpp)
target_link_libraries(test_raster_ndvi PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
  GDAL::GDAL
)
target_include_directories(test_raster_ndvi PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_raster_ndvi)

# Algorithm JSON schema and metadata tests
add_executable(test_algorithm_schema test_algorithm_schema.cpp)
target_link_libraries(test_algorithm_schema PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
  GDAL::GDAL
)
target_include_directories(test_algorithm_schema PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_algorithm_schema)

# Placeholder Grammar tests
add_executable(test_placeholder_grammar test_placeholder_grammar.cpp)
target_link_libraries(test_placeholder_grammar PRIVATE
  Catch2::Catch2WithMain
  sicnu_workflow
  sicnu_workflow_runtime
)
target_include_directories(test_placeholder_grammar PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_placeholder_grammar)

# GuiJobAdapter tests
add_executable(test_gui_job_adapter test_gui_job_adapter.cpp ../src/app/shell/gui_job_adapter.cpp)
target_link_libraries(test_gui_job_adapter PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Widgets
  sicnu_task_center
)
target_include_directories(test_gui_job_adapter PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_gui_job_adapter)

# MCP server tests
add_executable(test_mcp_server test_mcp_server.cpp ../src/agent/mcp_server.cpp ../src/app/shell/processing_job_adapter.cpp)
target_link_libraries(test_mcp_server PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
  sicnu_task_center
  sicnu_data
  sicnu_agent
  GDAL::GDAL
)
if(TARGET sicnu_operators)
  target_link_libraries(test_mcp_server PRIVATE sicnu_operators)
endif()
if(TARGET jsoncpp)
  target_link_libraries(test_mcp_server PRIVATE jsoncpp)
elseif(PkgConfig_FOUND)
  pkg_check_modules(JSONCPP QUIET jsoncpp)
  if(JSONCPP_FOUND)
    target_include_directories(test_mcp_server PRIVATE ${JSONCPP_INCLUDE_DIRS})
    target_link_libraries(test_mcp_server PRIVATE ${JSONCPP_LIBRARIES})
  else()
    target_link_libraries(test_mcp_server PRIVATE jsoncpp)
  endif()
else()
  target_link_libraries(test_mcp_server PRIVATE jsoncpp)
endif()
target_include_directories(test_mcp_server PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_mcp_server)

# Agent Tool Catalog tests
add_executable(test_agent_tool_catalog test_agent_tool_catalog.cpp)
target_link_libraries(test_agent_tool_catalog PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  sicnu_agent
  sicnu_processing
  sicnu_operators
  sicnu_data
  sicnu_task_center
)
if(TARGET jsoncpp)
  target_link_libraries(test_agent_tool_catalog PRIVATE jsoncpp)
endif()
target_include_directories(test_agent_tool_catalog PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_agent_tool_catalog)

# Spatial tool framework tests (ADR 0122)
sicnu_add_test(test_spatial_tools)

# Shipped-sidecar ↔ descriptor drift gate (#707) — dedicated binary so its
# RSOperatorRegistry / AtomicAlgorithmRegistry singletons are never polluted
# by other test cases' reset() calls.
sicnu_add_test(test_algorithm_meta_drift)

# Output verification for committed agent outputs
add_executable(test_output_verifier test_output_verifier.cpp)
target_link_libraries(test_output_verifier PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  sicnu_agent
  GDAL::GDAL
)
target_include_directories(test_output_verifier PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_output_verifier)

# Spatial Scientist 3.0 contract documents (ADR 0128) — light target, no QGIS GUI.
add_executable(test_spatial_contracts test_spatial_contracts.cpp)
target_link_libraries(test_spatial_contracts PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  sicnu_agent
)
sicnu_link_jsoncpp(test_spatial_contracts)
target_include_directories(test_spatial_contracts PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_spatial_contracts)

# Spatial Scientist 3.0 benchmark harness (Phase Q) — 100+ structured tasks.
qt_add_executable(test_spatial_scientist_benchmark
  test_spatial_scientist_benchmark.cpp
)
target_compile_definitions(test_spatial_scientist_benchmark PRIVATE
  SICNU_BENCHMARK_TASKS_JSON="${CMAKE_SOURCE_DIR}/data/benchmarks/spatial_scientist_tasks.json"
  SICNU_CARTOGRAPHY_DATA_DIR="${CMAKE_SOURCE_DIR}/data/cartography"
)
target_link_libraries(test_spatial_scientist_benchmark PRIVATE
  Catch2::Catch2
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_data
  sicnu_agent
  sicnu_operators
  sicnu_workflow
  sicnu_processing
  sicnu_task_center
)
if(TARGET jsoncpp)
  target_link_libraries(test_spatial_scientist_benchmark PRIVATE jsoncpp)
endif()
target_include_directories(test_spatial_scientist_benchmark PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_spatial_scientist_benchmark)


# Model catalog v2 contracts: readiness states, checksums, ranking gates
sicnu_add_test(test_model_catalog_v2)

# Platform 7.0 manifest surface: multimodal inputs, typed heads, provider
# contracts, closed-vocabulary unknown-key refusal (QtCore only).
sicnu_add_test(test_model_manifest7)

# Model runtime registry, ONNX provider, tile inference engine and rs:infer
# integration (needs OpenCV for the cv::dnn surface).
if(SICNU_HAS_OPENCV AND OpenCV_FOUND)
    add_executable(test_model_runtime test_model_runtime.cpp)
    target_link_libraries(test_model_runtime PRIVATE
        Catch2::Catch2WithMain
        Qt6::Core
        qgis_core
        sicnu_processing
        sicnu_task_center
        sicnu_operators
        ${OpenCV_LIBS}
    )
    sicnu_link_jsoncpp(test_model_runtime)
    target_include_directories(test_model_runtime PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}
    )
    sicnu_discover_tests(test_model_runtime)

    # Platform 7.0 runtime core: TensorBlob N-D value type, failure taxonomy
    # extension, inferNamed default bridge (needs OpenCV for the cv bridge).
    add_executable(test_tensor_blob test_tensor_blob.cpp)
    target_link_libraries(test_tensor_blob PRIVATE
        Catch2::Catch2WithMain
        Qt6::Core
        qgis_core
        sicnu_processing
        sicnu_task_center
        sicnu_operators
        ${OpenCV_LIBS}
    )
    sicnu_link_jsoncpp(test_tensor_blob)
    target_include_directories(test_tensor_blob PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}
    )
    sicnu_discover_tests(test_tensor_blob)

    # Platform 7.0 device planner: inventory, VRAM ledger, admission and
    # memory-pressure eviction (injectable inventory — runs without GPUs).
    add_executable(test_device_planner test_device_planner.cpp)
    target_link_libraries(test_device_planner PRIVATE
        Catch2::Catch2WithMain
        Qt6::Core
        qgis_core
        sicnu_processing
        sicnu_task_center
        sicnu_operators
        ${OpenCV_LIBS}
    )
    sicnu_link_jsoncpp(test_device_planner)
    target_include_directories(test_device_planner PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}
    )
    sicnu_discover_tests(test_device_planner)

    # Platform 7.0 multimodal / temporal tiled inference: multi-file same-grid
    # assembly, temporal stacks, coverage gate, service routing.
    add_executable(test_multimodal_inference test_multimodal_inference.cpp)
    target_link_libraries(test_multimodal_inference PRIVATE
        Catch2::Catch2WithMain
        Qt6::Core
        qgis_core
        sicnu_processing
        sicnu_task_center
        sicnu_operators
        ${OpenCV_LIBS}
    )
    sicnu_link_jsoncpp(test_multimodal_inference)
    target_include_directories(test_multimodal_inference PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/tests
        ${CMAKE_BINARY_DIR}
    )
    sicnu_discover_tests(test_multimodal_inference)

    # Platform 7.0 external provider contracts (same registry seam).
    add_executable(test_provider_python test_provider_python.cpp)
    target_link_libraries(test_provider_python PRIVATE
        Catch2::Catch2WithMain
        Qt6::Core
        qgis_core
        sicnu_processing
        sicnu_task_center
        sicnu_operators
        ${OpenCV_LIBS}
    )
    sicnu_link_jsoncpp(test_provider_python)
    target_include_directories(test_provider_python PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}
    )
    sicnu_discover_tests(test_provider_python)

    if(TARGET Qt6::Network)
        add_executable(test_provider_http test_provider_http.cpp)
        target_link_libraries(test_provider_http PRIVATE
            Catch2::Catch2WithMain
            Qt6::Core
            Qt6::Network
            qgis_core
            sicnu_processing
            sicnu_task_center
            sicnu_operators
            ${OpenCV_LIBS}
        )
        sicnu_link_jsoncpp(test_provider_http)
        target_include_directories(test_provider_http PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_BINARY_DIR}
        )
        sicnu_discover_tests(test_provider_http)
    endif()
endif()

# Platform 4.0: failure matrix + device/pool/registry contracts
if(SICNU_HAS_OPENCV AND OpenCV_FOUND)
    add_executable(test_model_failure_matrix test_model_failure_matrix.cpp)
    target_link_libraries(test_model_failure_matrix PRIVATE
        Catch2::Catch2WithMain
        Qt6::Core
        qgis_core
        sicnu_processing
        sicnu_task_center
        sicnu_operators
        ${OpenCV_LIBS}
    )
    sicnu_link_jsoncpp(test_model_failure_matrix)
    target_include_directories(test_model_failure_matrix PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/tests
        ${CMAKE_BINARY_DIR}
    )
    sicnu_discover_tests(test_model_failure_matrix)

    add_executable(test_model_tasks test_model_tasks.cpp)
    target_link_libraries(test_model_tasks PRIVATE
        Catch2::Catch2WithMain
        Qt6::Core
        qgis_core
        sicnu_processing
        sicnu_task_center
        sicnu_operators
        ${OpenCV_LIBS}
    )
    sicnu_link_jsoncpp(test_model_tasks)
    target_include_directories(test_model_tasks PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/tests
        ${CMAKE_BINARY_DIR}
    )
    sicnu_discover_tests(test_model_tasks)
endif()

# Platform 4.0: model runtime benchmark (env-gated: SICNU_MODEL_BENCH=1)
if(SICNU_HAS_OPENCV AND OpenCV_FOUND)
    add_executable(test_model_runtime_bench test_model_runtime_bench.cpp)
    target_link_libraries(test_model_runtime_bench PRIVATE
        Catch2::Catch2WithMain
        Qt6::Core
        qgis_core
        sicnu_processing
        sicnu_task_center
        sicnu_operators
        ${OpenCV_LIBS}
    )
    sicnu_link_jsoncpp(test_model_runtime_bench)
    target_include_directories(test_model_runtime_bench PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/tests
        ${CMAKE_BINARY_DIR}
    )
    sicnu_discover_tests(test_model_runtime_bench)
endif()

# Unified datasource resolver contract (GH #560)
sicnu_add_test(test_datasource_resolver)

# GDAL error handler tests
add_executable(test_gdal_errors test_gdal_errors.cpp)
target_link_libraries(test_gdal_errors PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
  GDAL::GDAL
)
target_include_directories(test_gdal_errors PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_gdal_errors)

# GDAL tool wrapper tests — verify error message capture
add_executable(test_gdal_tool_wrapper test_gdal_tool_wrapper.cpp)
target_link_libraries(test_gdal_tool_wrapper PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
  GDAL::GDAL
)
target_include_directories(test_gdal_tool_wrapper PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_gdal_tool_wrapper)

# GDAL addo wrapper parameter tests
add_executable(test_gdaladdo_params test_gdaladdo_params.cpp)
target_link_libraries(test_gdaladdo_params PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
)
target_include_directories(test_gdaladdo_params PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_gdaladdo_params)

# GDAL edit/pct2rgb/rgb2pct/gdal2xyz wrapper parameter tests
add_executable(test_gdal_edit_params test_gdal_edit_params.cpp)
target_link_libraries(test_gdal_edit_params PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
)
target_include_directories(test_gdal_edit_params PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_gdal_edit_params)

# GDAL null band handle tests
add_executable(test_gdal_null_band test_gdal_null_band.cpp)
target_link_libraries(test_gdal_null_band PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
  GDAL::GDAL
)
target_include_directories(test_gdal_null_band PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_gdal_null_band)

# GDAL thread safety tests
add_executable(test_gdal_thread_safety test_gdal_thread_safety.cpp)
target_link_libraries(test_gdal_thread_safety PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
  GDAL::GDAL
)
target_include_directories(test_gdal_thread_safety PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_gdal_thread_safety)

# GDAL wrapper tests — verify GDAL C API wrapper
add_executable(test_gdal_wrapper test_gdal_wrapper.cpp)
target_link_libraries(test_gdal_wrapper PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
  GDAL::GDAL
)
target_include_directories(test_gdal_wrapper PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_gdal_wrapper)

# Out-of-core GDAL block-streaming iterator tests
add_executable(test_gdal_block_stream test_gdal_block_stream.cpp)
target_link_libraries(test_gdal_block_stream PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
  GDAL::GDAL
)
target_include_directories(test_gdal_block_stream PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_gdal_block_stream)

# Multi-band streaming primitives (Phase E1): BIP tile read + streaming output writer.
add_executable(test_gdal_multiband_block_stream test_gdal_multiband_block_stream.cpp)
target_link_libraries(test_gdal_multiband_block_stream PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
  GDAL::GDAL
)
target_include_directories(test_gdal_multiband_block_stream PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_gdal_multiband_block_stream)

# Performance benchmarks (Phase B of the perf/architecture goal). Runnable via
# ctest (small sizes) or directly with SICNU_BENCH_LARGE=1 for nightly profiling.
# Uses the standard sicnu_add_test link set (operators + task_center + resource_monitor).
sicnu_add_test(test_perf_benchmarks TIMEOUT 300 RUN_SERIAL TRUE)

# Execution & Data Plane 3.0 benchmark harness (Phase A of the execution-data-
# plane-3 epic): in-process workloads through TaskCenter/JobEngine/operators
# with JSON output (SICNU_EXEC_BENCH_OUT=<dir>) merged by scripts/perf_report.py.
# RUN_SERIAL: measures peak RSS deltas and IO counters of this process.
# The remote-source fixtures (tests/support/mini_cog_server.h) use plain
# blocking POSIX sockets; Windows lane skips these two targets (see REVIEW_LOG).
if(NOT WIN32)
sicnu_add_test(test_execution_benchmarks TIMEOUT 600 RUN_SERIAL TRUE)
endif()

# Phase B chunk execution graph contracts (Qt-free; links sicnu_runtime only).
sicnu_add_test(test_chunk_graph TIMEOUT 120)
target_link_libraries(test_chunk_graph PRIVATE sicnu_runtime)

# Phase D artifact identity store (Phase D of the epic). SQLite link for the
# schema-guard test's direct DB surgery; sicnu_data keeps its own PRIVATE link.
sicnu_add_test(test_artifact_store TIMEOUT 120)
target_link_libraries(test_artifact_store PRIVATE SQLite::SQLite3)

# Foundation 5.0 dataset core (ADR 0134): identity/manifest/fingerprint/
# version lifecycle/store. SQLite link mirrors test_artifact_store (the
# schema-guard test pokes the DB directly).
sicnu_add_test(test_dataset_core TIMEOUT 120)
target_link_libraries(test_dataset_core PRIVATE Sicnu::dataset SQLite::SQLite3)

# Foundation 5.0 sample model / label ontology / annotation chains (ADR 0135).
sicnu_add_test(test_sample_label_annotation TIMEOUT 120)
target_link_libraries(test_sample_label_annotation PRIVATE Sicnu::dataset SQLite::SQLite3)

# Foundation 5.0 deterministic splits + leakage audit + patch generator
# known-answer tests (ADR 0136, goal §19/§51).
sicnu_add_test(test_split_leakage TIMEOUT 120)
target_link_libraries(test_split_leakage PRIVATE Sicnu::dataset SQLite::SQLite3)

# Foundation 5.0 experiment/run identity, metrics, comparison, lineage and
# reproduction bundle (ADR 0137/0138, goal §23-§31/§51).
sicnu_add_test(test_experiment_evaluation TIMEOUT 180)
target_link_libraries(test_experiment_evaluation PRIVATE Sicnu::experiment Sicnu::dataset SQLite::SQLite3)

# Foundation 5.0 dataset composition/label QA + the 100k metadata scale
# contract (goal §20/§37/§46/§52). RUN_SERIAL: the 100k benchmark should not
# compete with other tests for memory.
sicnu_add_test(test_dataset_quality_scale TIMEOUT 600)
target_link_libraries(test_dataset_quality_scale PRIVATE Sicnu::dataset SQLite::SQLite3)
set_property(TARGET test_dataset_quality_scale PROPERTY RUN_SERIAL TRUE)

# Foundation 5.0 end-to-end examples (goal §54): optical classification,
# SAR pair, multimodal temporal, segmentation — small data, whole chain.
sicnu_add_test(test_dataset_e2e_examples TIMEOUT 180)
target_link_libraries(test_dataset_e2e_examples PRIVATE Sicnu::experiment Sicnu::dataset SQLite::SQLite3)

sicnu_add_test(test_adversarial_m2 TIMEOUT 180)
target_link_libraries(test_adversarial_m2 PRIVATE Sicnu::experiment Sicnu::dataset SQLite::SQLite3)

# Platform 7.0 dataset/experiment surface: split-manifest + leakage-report
# persistence, MCP tool projections, honest reproducibility levels.
sicnu_add_test(test_data_platform_surface TIMEOUT 300)
target_link_libraries(test_data_platform_surface PRIVATE Sicnu::experiment Sicnu::dataset SQLite::SQLite3)

# Platform 7.0 library contracts: promotion, fold audit, facets, replay
# readiness, comparison extensions.
sicnu_add_test(test_platform7_library TIMEOUT 600)
target_link_libraries(test_platform7_library PRIVATE Sicnu::experiment Sicnu::dataset SQLite::SQLite3)
set_property(TARGET test_platform7_library PROPERTY RUN_SERIAL TRUE)


# Phase C fused-chain equivalence (links through sicnu_task_center which
# carries fused_chain.cpp; needs the standard set only).
sicnu_add_test(test_fused_chain TIMEOUT 300)

# Phase F remote COG caching layer (network-free data layer; POSIX sockets
# provide the fake remote endpoint).
if(NOT WIN32)
sicnu_add_test(test_remote_source_cache TIMEOUT 180)
endif()
if(NOT WIN32)
target_include_directories(test_remote_source_cache PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(test_execution_benchmarks PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
endif()

# Phase G scheduler contracts (header-only budget2 + tests).
sicnu_add_test(test_scheduler3 TIMEOUT 120)

# Phase H GPU session plane (Qt-free; fake backend).
sicnu_add_test(test_gpu_plane TIMEOUT 120)
target_link_libraries(test_gpu_plane PRIVATE sicnu_runtime)

# Phase K worker isolation (executes the real sicnu_worker binary; RUN_SERIAL
# so SIGKILL/kill escalation never disturbs neighboring tests).
sicnu_add_test(test_worker_host TIMEOUT 300 RUN_SERIAL TRUE)
target_compile_definitions(test_worker_host PRIVATE
    SICNU_WORKER_EXE="$<TARGET_FILE:sicnu_worker>"
)

# Execution Plane 7.0 fault matrix (retry/routing/admission/cancel/shutdown/
# stress). RUN_SERIAL: the sticky-shutdown case must run after everything and
# the routing cases spawn real worker processes.
sicnu_add_test(test_execution_plane_7 TIMEOUT 1200 RUN_SERIAL TRUE)
target_compile_definitions(test_execution_plane_7 PRIVATE
    SICNU_WORKER_EXE="$<TARGET_FILE:sicnu_worker>"
)

# Phase I persistent workspace catalog.
sicnu_add_test(test_workspace_catalog TIMEOUT 300)

# Workspace Governance 3.0 — persistent governance store.
sicnu_add_test(test_governance_store TIMEOUT 300)
sicnu_add_test(test_workspace_services TIMEOUT 600)
# Workspace Governance 3.0 — scale contract (SICNU_WS3_STRESS=1 -> 100k).
sicnu_add_test(test_workspace_stress TIMEOUT 900 RUN_SERIAL TRUE)

# Workbench 5.0 — UI scale certification (Milestone O): the governance model
# pipeline the workspace browser uses, at 10k rows by default and 100k under
# SICNU_WS3_STRESS=1. Compiles the panel translation unit for the model.
add_executable(test_ui_scale_benchmark
  test_ui_scale_benchmark.cpp
  ${CMAKE_SOURCE_DIR}/src/app/panels/workspace_browser_panel.cpp
)
target_link_libraries(test_ui_scale_benchmark PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_data
  sicnu_processing
  sicnu_jobs
  sicnu_task_center
  sicnu_agent
  sicnu_operators
  sicnu_workflow_runtime
)
target_include_directories(test_ui_scale_benchmark PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
target_compile_definitions(test_ui_scale_benchmark PRIVATE
  CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
if(TARGET Sicnu::session_map)
  target_link_libraries(test_ui_scale_benchmark PRIVATE Sicnu::session_map)
endif()
sicnu_discover_tests(test_ui_scale_benchmark TIMEOUT 900 RUN_SERIAL TRUE)
sicnu_add_test(test_governance_tools TIMEOUT 300)

# Phase M fault injection / chaos matrix (fork + kill semantics; serial).
# fork()/waitpid()-based crash-injection fixture is POSIX-only.
if(NOT WIN32)
sicnu_add_test(test_fault_injection TIMEOUT 300 RUN_SERIAL TRUE)
endif()
# #706: the python-bindings smoke test existed but was registered nowhere —
# it silently never ran in any lane. Guarded on the bindings target existing.
if(TARGET _sicnu_operators AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/test_python_bindings.py)
    set(_sicnu_py ${Python_EXECUTABLE})
    if(NOT _sicnu_py)
        set(_sicnu_py python3)
    endif()
    add_test(
        NAME python_sdk_unit
        COMMAND ${_sicnu_py} ${CMAKE_CURRENT_SOURCE_DIR}/test_python_sdk_unit.py
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
    set_tests_properties(python_sdk_unit PROPERTIES TIMEOUT 120)
    sicnu_set_test_env(python_sdk_unit)
    add_test(
        NAME python_bindings_smoke
        COMMAND ${_sicnu_py} ${CMAKE_CURRENT_SOURCE_DIR}/test_python_bindings.py
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
    set_tests_properties(python_bindings_smoke PROPERTIES TIMEOUT 300)
    sicnu_set_test_env(python_bindings_smoke)
endif()

# #706: pi/ (ADR 0122) ships bridge lifecycle regression tests as node --test
# modules (the #669 respawn/fork-loop family), but like the python-bindings
# smoke test they ran in no lane. Register them in ctest when a capable node
# is available: the test imports the .ts bridge directly, which requires
# type stripping (default-on only on node >= 22.18 / >= 23.6).
find_program(SICNU_NODE_EXECUTABLE node)
if(SICNU_NODE_EXECUTABLE AND EXISTS ${CMAKE_SOURCE_DIR}/pi/test/mcp_bridge.test.mjs)
    execute_process(
        COMMAND ${SICNU_NODE_EXECUTABLE} --version
        OUTPUT_VARIABLE _sicnu_node_version
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    if(_sicnu_node_version MATCHES "^v([0-9]+)\\.([0-9]+)")
        set(_sicnu_node_major ${CMAKE_MATCH_1})
        set(_sicnu_node_minor ${CMAKE_MATCH_2})
        if(_sicnu_node_major GREATER 23
           OR (_sicnu_node_major EQUAL 23 AND _sicnu_node_minor GREATER_EQUAL 6)
           OR (_sicnu_node_major EQUAL 22 AND _sicnu_node_minor GREATER_EQUAL 18))
            add_test(
                NAME pi_bridge_lifecycle
                COMMAND ${SICNU_NODE_EXECUTABLE} --test test/mcp_bridge.test.mjs
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/pi)
            set_tests_properties(pi_bridge_lifecycle PROPERTIES TIMEOUT 120)
            sicnu_set_test_env(pi_bridge_lifecycle)
        else()
            message(STATUS "node ${_sicnu_node_version} lacks default type stripping — pi bridge lifecycle tests skipped")
        endif()
    else()
        message(STATUS "unparseable node version '${_sicnu_node_version}' — pi bridge lifecycle tests skipped")
    endif()
    unset(_sicnu_node_version)
    unset(_sicnu_node_major)
    unset(_sicnu_node_minor)
endif()


# Resource-aware scheduler tests (Phase C): pure TaskResourceBudget unit tests +
# TaskCenter integration of the RAM-budget launch gate.
sicnu_add_test(test_task_resource_budget)

# Revision-aware execution cache (Phase F): fingerprint determinism/sensitivity +
# cache opt-in/lookup/store/invalidate/invalidation.
sicnu_add_test(test_execution_fingerprint)

# Colormap tests — verify color ramps loaded from style database
# Custom main: needs QgsApplication with correct prefix path
add_executable(test_colormap test_colormap.cpp)
target_link_libraries(test_colormap PRIVATE
  Catch2::Catch2
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
)
target_include_directories(test_colormap PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_colormap)

# Processing tests — link sicnu_processing for provider/algorithm tests
add_executable(test_processing test_processing.cpp)
target_link_libraries(test_processing PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
  sicnu_operators
)
target_include_directories(test_processing PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_processing)

# Algorithm organization tests — verify metadata quality
add_executable(test_algorithm_organization test_algorithm_organization.cpp)
target_link_libraries(test_algorithm_organization PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
  sicnu_operators
)
target_include_directories(test_algorithm_organization PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_algorithm_organization)

# Toolbox coverage tests — manifest-driven algorithm coverage CI gate
add_executable(test_toolbox_coverage test_toolbox_coverage.cpp)
target_link_libraries(test_toolbox_coverage PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
  sicnu_operators
)
target_include_directories(test_toolbox_coverage PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_toolbox_coverage)

# Processing Toolbox Phase 1 Task 7 — Generic CLI shipped tools
add_executable(test_generic_cli_manifest test_generic_cli_manifest.cpp)
target_link_libraries(test_generic_cli_manifest PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
  sicnu_operators
)
target_include_directories(test_generic_cli_manifest PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_generic_cli_manifest)

# Progress dialog tests
add_executable(test_progress_dialog test_progress_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/progress_dialog.cpp
)
target_link_libraries(test_progress_dialog PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
)
target_include_directories(test_progress_dialog PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_progress_dialog)

# Spectral profile widget tests
add_executable(test_spectral_profile_widget test_spectral_profile_widget.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/spectral_profile_widget.cpp
  ${CMAKE_SOURCE_DIR}/src/processing/gdal/gdal_dataset_wrapper.cpp
)
target_link_libraries(test_spectral_profile_widget PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
  GDAL::GDAL
)
target_include_directories(test_spectral_profile_widget PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_spectral_profile_widget)

# ROI statistics widget tests (#429)
add_executable(test_roi_statistics_widget test_roi_statistics_widget.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/roi_statistics_widget.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/rs_scan_pool.cpp
  ${CMAKE_SOURCE_DIR}/src/processing/gdal/gdal_dataset_wrapper.cpp
)
target_link_libraries(test_roi_statistics_widget PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
  GDAL::GDAL
)
target_include_directories(test_roi_statistics_widget PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_roi_statistics_widget)

# Comparison widget tests
add_executable(test_comparison_widget test_comparison_widget.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/comparison_widget.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/comparison_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/dialog_help_catalog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/dialog_utils.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/raster_layer_combo.cpp
  ${CMAKE_SOURCE_DIR}/src/processing/gdal/gdal_dataset_wrapper.cpp
)
target_link_libraries(test_comparison_widget PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
  sicnu_data
  GDAL::GDAL
)
target_include_directories(test_comparison_widget PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_comparison_widget)

# Panel persistence tests
add_executable(test_panel_persistence test_panel_persistence.cpp)
target_link_libraries(test_panel_persistence PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
)
target_include_directories(test_panel_persistence PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_panel_persistence)

# Preferences dialog tests
add_executable(test_preferences_dialog
  test_preferences_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/preferences_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/dialog_help_catalog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/dialog_utils.cpp
)
target_link_libraries(test_preferences_dialog PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
  qgis_core
  qgis_gui
  sicnu_processing
  sicnu_data
  GDAL::GDAL
)
target_include_directories(test_preferences_dialog PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_preferences_dialog PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_preferences_dialog)

# Processing dialog tests
add_executable(test_processing_dialog test_processing_dialog.cpp)
target_link_libraries(test_processing_dialog PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
)
target_include_directories(test_processing_dialog PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_processing_dialog)

# Advanced algorithms tests
add_executable(test_advanced_algorithms test_advanced_algorithms.cpp)
target_link_libraries(test_advanced_algorithms PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
)
target_include_directories(test_advanced_algorithms PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_advanced_algorithms)

# Data format tests (GDAL drivers)
add_executable(test_data_formats test_data_formats.cpp)
target_link_libraries(test_data_formats PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  GDAL::GDAL
)
target_include_directories(test_data_formats PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_data_formats)

# Vector layer properties tests
sicnu_add_test(test_vector_properties)

# Layer tree bridge tests
sicnu_add_test(test_layer_tree_bridge)

# Workbench 5.0 — layer state-sync contract (Milestone E)
add_executable(test_layer_sync_contract
  test_layer_sync_contract.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/selection_context.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/workbench_host.cpp
)
target_link_libraries(test_layer_sync_contract PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
)
target_include_directories(test_layer_sync_contract PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
target_compile_definitions(test_layer_sync_contract PRIVATE
  CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
sicnu_discover_tests(test_layer_sync_contract)

# Change detection tests
add_executable(test_change_detection test_change_detection.cpp)
target_link_libraries(test_change_detection PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
  sicnu_operators
)
sicnu_link_jsoncpp(test_change_detection)
target_include_directories(test_change_detection PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_change_detection)

# Post-classification transition matrix kernel tests
add_executable(test_post_classification_kernel test_post_classification_kernel.cpp)
target_link_libraries(test_post_classification_kernel PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
)
target_include_directories(test_post_classification_kernel PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_post_classification_kernel)

# Mosaic tests
add_executable(test_mosaic test_mosaic.cpp)
target_link_libraries(test_mosaic PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
)
target_include_directories(test_mosaic PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_mosaic)

# Processing integration tests
add_executable(test_processing_integration test_processing_integration.cpp)
target_link_libraries(test_processing_integration PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
)
target_include_directories(test_processing_integration PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_processing_integration)

# Log panel tests
add_executable(test_log_panel test_log_panel.cpp
  ${CMAKE_SOURCE_DIR}/src/app/log_panel.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/rs_empty_state_widget.cpp
  ${CMAKE_SOURCE_DIR}/tests/test_log_panel_main.cpp
)
target_link_libraries(test_log_panel PRIVATE
  Catch2::Catch2
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
)
target_include_directories(test_log_panel PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_log_panel)

# Image enhancement tests
add_executable(test_image_enhancement test_image_enhancement.cpp)
target_link_libraries(test_image_enhancement PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
)
target_include_directories(test_image_enhancement PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_image_enhancement)

# Histogram equalize edge case tests
add_executable(test_histogram_equalize_edge_cases test_histogram_equalize_edge_cases.cpp)
target_link_libraries(test_histogram_equalize_edge_cases PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
)
target_include_directories(test_histogram_equalize_edge_cases PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_histogram_equalize_edge_cases)

# Terrain analysis tests (Phase 11.2)
add_executable(test_terrain test_terrain.cpp)
target_link_libraries(test_terrain PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
)
target_include_directories(test_terrain PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_terrain)

# Image fusion tests (Phase 11.1)
add_executable(test_image_fusion test_image_fusion.cpp)
target_link_libraries(test_image_fusion PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
)
target_include_directories(test_image_fusion PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_image_fusion)

# PCA tests
add_executable(test_pca test_pca.cpp)
target_link_libraries(test_pca PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
  GDAL::GDAL
)
target_include_directories(test_pca PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_pca)

# Spatial filter tests
add_executable(test_spatial_filter test_spatial_filter.cpp)
target_link_libraries(test_spatial_filter PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
)
target_include_directories(test_spatial_filter PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_spatial_filter)

# SAR speckle filter tests (Lee, Frost, Kuan, Gamma-MAP)
add_executable(test_speckle_filter test_speckle_filter.cpp)
target_link_libraries(test_speckle_filter PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
)
target_include_directories(test_speckle_filter PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_speckle_filter)

# Georeferencer least-squares tests
add_executable(test_least_squares test_least_squares.cpp)
target_link_libraries(test_least_squares PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  qgis_analysis
)
target_include_directories(test_least_squares PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/georeferencing
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_least_squares)

# Georeferencer GCP transformer tests
add_executable(test_gcp_transformer test_gcp_transformer.cpp)
target_link_libraries(test_gcp_transformer PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  qgis_analysis
)
target_include_directories(test_gcp_transformer PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/georeferencing
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_gcp_transformer)

# Georeferencer image warper tests (Task 11.4.2)
add_executable(test_image_warper test_image_warper.cpp)
target_link_libraries(test_image_warper PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  qgis_core
  qgis_analysis
  qgis_app_georef
  GDAL::GDAL
)
target_include_directories(test_image_warper PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/georeferencing
  ${CMAKE_SOURCE_DIR}/src/app/georeferencer
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_image_warper)

add_executable(test_image_warper_cancel test_image_warper_cancel.cpp)
target_link_libraries(test_image_warper_cancel PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Concurrent
  qgis_core
  qgis_analysis
  qgis_app_georef
  GDAL::GDAL
)
target_include_directories(test_image_warper_cancel PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/georeferencing
  ${CMAKE_SOURCE_DIR}/src/app/georeferencer
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_image_warper_cancel)

add_executable(test_image_warper_errors test_image_warper_errors.cpp)
target_link_libraries(test_image_warper_errors PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  qgis_core
  qgis_analysis
  qgis_app_georef
  GDAL::GDAL
)
target_include_directories(test_image_warper_errors PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/georeferencing
  ${CMAKE_SOURCE_DIR}/src/app/georeferencer
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_image_warper_errors)

add_executable(test_warp_crs_passthrough test_warp_crs_passthrough.cpp)
target_link_libraries(test_warp_crs_passthrough PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  qgis_core
  qgis_analysis
  qgis_app_georef
  GDAL::GDAL
)
target_include_directories(test_warp_crs_passthrough PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/georeferencing
  ${CMAKE_SOURCE_DIR}/src/app/georeferencer
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_warp_crs_passthrough)

# Georeferencer GCP list tests (Task 11.4.3) — signal emission, residual skip-disabled
add_executable(test_gcp_list test_gcp_list.cpp)
target_link_libraries(test_gcp_list PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Test
  qgis_core
  qgis_analysis
  qgis_app_georef
)
target_include_directories(test_gcp_list PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/georeferencing
  ${CMAKE_SOURCE_DIR}/src/app/georeferencer
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_gcp_list)

# Georeferencer .points v2 file tests (Task 11.4.3)
add_executable(test_gcp_points_file test_gcp_points_file.cpp)
target_link_libraries(test_gcp_points_file PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  qgis_analysis
  qgis_app_georef
)
target_include_directories(test_gcp_points_file PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/georeferencing
  ${CMAKE_SOURCE_DIR}/src/app/georeferencer
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_gcp_points_file)

# Georeferencer main window shell tests (Task 11.4.4)
add_executable(test_georef_window test_georef_window.cpp)
target_link_libraries(test_georef_window PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
  qgis_core
  qgis_analysis
  qgis_app_georef
)
target_include_directories(test_georef_window PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/georeferencing
  ${CMAKE_SOURCE_DIR}/src/app/georeferencer
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_georef_window PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_georef_window)

# Dual-window redesign — I2I + I2M shell smoke tests
add_executable(test_georef_dual_window test_georef_dual_window.cpp)
target_link_libraries(test_georef_dual_window PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
  qgis_core
  qgis_analysis
  qgis_app_georef
  sicnu_data
)
target_include_directories(test_georef_dual_window PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/georeferencing
  ${CMAKE_SOURCE_DIR}/src/app/georeferencer
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_georef_dual_window PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_georef_dual_window)

# #33 — I2I / I2M adapters share Georeferencing Session type with independence
add_executable(test_georef_session_adapters
  test_georef_session_adapters.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/dialog_utils.cpp
)
target_link_libraries(test_georef_session_adapters PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_analysis
  qgis_app_georef
  sicnu_jobs
  sicnu_task_center
  sicnu_data
)
target_include_directories(test_georef_session_adapters PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/georeferencing
  ${CMAKE_SOURCE_DIR}/src/app/georeferencer
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_georef_session_adapters PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_georef_session_adapters)

# Georef task list (Apply → enqueue UI)
add_executable(test_georef_task_list test_georef_task_list.cpp)
target_link_libraries(test_georef_task_list PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
  qgis_core
  qgis_analysis
  qgis_app_georef
)
target_include_directories(test_georef_task_list PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/georeferencing
  ${CMAKE_SOURCE_DIR}/src/app/georeferencer
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_georef_task_list PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_georef_task_list)

# Task 5 (v1.6) — mode-aware MapCoords pick canvas
add_executable(test_pick_canvas_mode test_pick_canvas_mode.cpp)
target_link_libraries(test_pick_canvas_mode PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
  qgis_core
  qgis_analysis
  qgis_app_georef
)
target_include_directories(test_pick_canvas_mode PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/georeferencing
  ${CMAKE_SOURCE_DIR}/src/app/georeferencer
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_pick_canvas_mode PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_pick_canvas_mode)

# GCP table widget tests (Task 11.4.6)
add_executable(test_gcp_list_widget test_gcp_list_widget.cpp)
target_link_libraries(test_gcp_list_widget PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
  qgis_core
  qgis_analysis
  qgis_app_georef
)
target_include_directories(test_gcp_list_widget PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/georeferencing
  ${CMAKE_SOURCE_DIR}/src/app/georeferencer
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_gcp_list_widget PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_gcp_list_widget)

# Twin canvas sync controller tests (Task 11.4.5)
add_executable(test_twincanvas_sync test_twincanvas_sync.cpp)
target_link_libraries(test_twincanvas_sync PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
  qgis_core
  qgis_gui
  qgis_analysis
  qgis_app_georef
)
target_include_directories(test_twincanvas_sync PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/georeferencing
  ${CMAKE_SOURCE_DIR}/src/app/georeferencer
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_twincanvas_sync)

# Dual 1x2 viewport pan/zoom sync controller tests
add_executable(test_dual_viewport_sync
  test_dual_viewport_sync.cpp
  ${CMAKE_SOURCE_DIR}/src/app/shell/rs_dual_viewport_sync_controller.cpp
)
target_link_libraries(test_dual_viewport_sync PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
  qgis_core
  qgis_gui
)
target_include_directories(test_dual_viewport_sync PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_dual_viewport_sync)

# Swipe (curtain) comparison map tool tests
add_executable(test_swipe_map_tool test_swipe_map_tool.cpp
  ${CMAKE_SOURCE_DIR}/src/app/map_tools/swipe_map_tool.cpp
)
target_link_libraries(test_swipe_map_tool PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
  qgis_core
  qgis_gui
)
target_include_directories(test_swipe_map_tool PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_swipe_map_tool)

# Measure map tool tests (#318)
add_executable(test_measure_tool test_measure_tool.cpp
  ${CMAKE_SOURCE_DIR}/src/app/map_tools/measure_tool.cpp
)
target_link_libraries(test_measure_tool PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
  qgis_core
  qgis_gui
)
target_include_directories(test_measure_tool PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_measure_tool)

# ROI spectrum tool tests (#322)
add_executable(test_roi_spectrum_tool test_roi_spectrum_tool.cpp
  ${CMAKE_SOURCE_DIR}/src/app/map_tools/rs_roi_spectrum_tool.cpp
)
target_link_libraries(test_roi_spectrum_tool PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
  Qt6::Concurrent
  sicnu_processing
  qgis_core
  qgis_gui
  GDAL::GDAL
)
target_include_directories(test_roi_spectrum_tool PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_roi_spectrum_tool)

# Vector layer properties lazy statistics tests (#324)
add_executable(test_vector_layer_properties_lazy test_vector_layer_properties_lazy.cpp)
target_link_libraries(test_vector_layer_properties_lazy PRIVATE
  Catch2::Catch2
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
  qgis_core
  qgis_gui
)
target_include_directories(test_vector_layer_properties_lazy PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/gui
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_vector_layer_properties_lazy)

# RMS scatter widget tests (Task 11.4.7)
add_executable(test_rms_scatter test_rms_scatter.cpp)
target_link_libraries(test_rms_scatter PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
  qgis_core
  qgis_analysis
  qgis_app_georef
)
target_include_directories(test_rms_scatter PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/georeferencing
  ${CMAKE_SOURCE_DIR}/src/app/georeferencer
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_rms_scatter)

# Warp-in-progress UI lock tests (Task 11.4.7)
add_executable(test_georef_window_warp_lock test_georef_window_warp_lock.cpp)
target_link_libraries(test_georef_window_warp_lock PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
  qgis_core
  qgis_analysis
  qgis_app_georef
)
target_include_directories(test_georef_window_warp_lock PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/georeferencing
  ${CMAKE_SOURCE_DIR}/src/app/georeferencer
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_georef_window_warp_lock PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_georef_window_warp_lock)

# Task 11.4.8 — RPC physical model transformer tests
add_executable(test_rpc_transformer test_rpc_transformer.cpp)
target_link_libraries(test_rpc_transformer PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  qgis_analysis
  GDAL::GDAL
  sicnu_data
)
target_include_directories(test_rpc_transformer PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/georeferencing
  ${CMAKE_SOURCE_DIR}/src/app/georeferencer
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_rpc_transformer)

# Task 11.4.8 — RPC mode UI test (DEM section visibility on toggle)
add_executable(test_georef_window_rpc_mode
  test_georef_window_rpc_mode.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/dialog_utils.cpp
)
target_link_libraries(test_georef_window_rpc_mode PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
  qgis_core
  qgis_analysis
  qgis_app_georef
  sicnu_jobs
  sicnu_data
)
target_include_directories(test_georef_window_rpc_mode PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/georeferencing
  ${CMAKE_SOURCE_DIR}/src/app/georeferencer
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_georef_window_rpc_mode PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_georef_window_rpc_mode)

# Task 11.5.4 — DEM Z-offset wired into RPC pipeline
add_executable(test_dem_z_offset test_dem_z_offset.cpp)
target_link_libraries(test_dem_z_offset PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  qgis_analysis
  GDAL::GDAL
)
target_include_directories(test_dem_z_offset PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/georeferencing
  ${CMAKE_SOURCE_DIR}/src/app/georeferencer
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_dem_z_offset)

# Task 11.5.5 — RPC GCP linear-bias refinement
add_executable(test_rpc_gcp_refine test_rpc_gcp_refine.cpp)
target_link_libraries(test_rpc_gcp_refine PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  qgis_analysis
  GDAL::GDAL
)
target_include_directories(test_rpc_gcp_refine PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/georeferencing
  ${CMAKE_SOURCE_DIR}/src/app/georeferencer
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_rpc_gcp_refine)

# Task 11.5.1 — CRS picker persistence + signal tests
add_executable(test_crs_picker_persists test_crs_picker_persists.cpp)
target_link_libraries(test_crs_picker_persists PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
  qgis_core
  qgis_gui
  qgis_analysis
  qgis_app_georef
)
target_include_directories(test_crs_picker_persists PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/georeferencing
  ${CMAKE_SOURCE_DIR}/src/app/georeferencer
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_crs_picker_persists PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_crs_picker_persists)

# Task 11.6.1 — Georef session state (dirty + settings)
# Compiles session_state.cpp directly (Qt-only unit) so the test does not
# require a full qgis_app_georef link chain. Source is still in the library.
# #32 — Georeferencing Session (GCP fit + immutable warp snapshot)
add_executable(test_georeferencing_session test_georeferencing_session.cpp)
target_link_libraries(test_georeferencing_session PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  qgis_core
  qgis_analysis
  qgis_app_georef
  sicnu_jobs
  sicnu_task_center
  sicnu_workflow_runtime
  GDAL::GDAL
)
target_include_directories(test_georeferencing_session PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_SOURCE_DIR}/src/app/georeferencer
  ${CMAKE_SOURCE_DIR}/src/analysis/georeferencing
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_georeferencing_session PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_georeferencing_session)

# #34 — SIFT / template match Task Center seams + Session thin push
add_executable(test_georef_match_task_center test_georef_match_task_center.cpp)
target_link_libraries(test_georef_match_task_center PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  qgis_core
  qgis_analysis
  qgis_app_georef
  sicnu_jobs
  sicnu_task_center
)
target_include_directories(test_georef_match_task_center PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_SOURCE_DIR}/src/app/georeferencer
  ${CMAKE_SOURCE_DIR}/src/analysis/georeferencing
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_georef_match_task_center PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_georef_match_task_center)

add_executable(test_georef_session_state
  test_georef_session_state.cpp
)
target_link_libraries(test_georef_session_state PRIVATE
  Catch2::Catch2WithMain
  qgis_app_georef
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
)
target_include_directories(test_georef_session_state PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app/georeferencer
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_georef_session_state)

# Task 11.6.3 — GCP hit-test (contains)
add_executable(test_gcp_contains test_gcp_contains.cpp)
target_link_libraries(test_gcp_contains PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
  qgis_core
  qgis_gui
  qgis_analysis
  qgis_app_georef
)
target_include_directories(test_gcp_contains PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/georeferencing
  ${CMAKE_SOURCE_DIR}/src/app/georeferencer
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_gcp_contains PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_gcp_contains)

# Task 11.5.3 — Image-to-Image mode load test
add_executable(test_image_to_image_load test_image_to_image_load.cpp)
target_link_libraries(test_image_to_image_load PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
  qgis_core
  qgis_gui
  qgis_analysis
  qgis_app_georef
  GDAL::GDAL
)
target_include_directories(test_image_to_image_load PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/georeferencing
  ${CMAKE_SOURCE_DIR}/src/app/georeferencer
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_image_to_image_load PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_image_to_image_load)

# Task 11.5.2 — GCP canvas item paint smoke test
add_executable(test_gcp_canvas_item test_gcp_canvas_item.cpp)
target_link_libraries(test_gcp_canvas_item PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
  qgis_core
  qgis_gui
  qgis_analysis
  qgis_app_georef
)
target_include_directories(test_gcp_canvas_item PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/georeferencing
  ${CMAKE_SOURCE_DIR}/src/app/georeferencer
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_gcp_canvas_item PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_gcp_canvas_item)

# Task 4 (v16) — REF GCP marker CRS reprojection onto canvas destination CRS
add_executable(test_gcp_canvas_crs test_gcp_canvas_crs.cpp)
target_link_libraries(test_gcp_canvas_crs PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
  qgis_core
  qgis_gui
  qgis_analysis
  qgis_app_georef
)
target_include_directories(test_gcp_canvas_crs PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/georeferencing
  ${CMAKE_SOURCE_DIR}/src/app/georeferencer
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_gcp_canvas_crs PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_gcp_canvas_crs)

# Task 11.5.6 — RPC golden warp regression (synthetic non-trivial RPC + DEM)
add_executable(test_rpc_golden test_rpc_golden.cpp)
target_compile_definitions(test_rpc_golden PRIVATE
  GEOREF_TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/tests/data/georef")
target_link_libraries(test_rpc_golden PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  qgis_core
  qgis_analysis
  qgis_app_georef
  GDAL::GDAL
)
target_include_directories(test_rpc_golden PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/georeferencing
  ${CMAKE_SOURCE_DIR}/src/app/georeferencer
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_rpc_golden)

# Task 11.5.7 — SIFT auto-match matcher tests
add_executable(test_sift_matcher test_sift_matcher.cpp)
target_link_libraries(test_sift_matcher PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  qgis_analysis
  qgis_app_georef
  GDAL::GDAL
)
target_include_directories(test_sift_matcher PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/georeferencing
  ${CMAKE_SOURCE_DIR}/src/app/georeferencer
  ${CMAKE_BINARY_DIR}
)
if(SICNU_HAS_OPENCV)
  target_compile_definitions(test_sift_matcher PRIVATE SICNU_HAS_OPENCV=1)
  target_link_libraries(test_sift_matcher PRIVATE ${OpenCV_LIBS})
  target_include_directories(test_sift_matcher PRIVATE ${OpenCV_INCLUDE_DIRS})
endif()
sicnu_discover_tests(test_sift_matcher)

# Phase 10A Task 10.1 — ROI collection unit tests
add_executable(test_roi_collection test_roi_collection.cpp)
target_link_libraries(test_roi_collection PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Test
  qgis_core
  qgis_analysis
)
target_include_directories(test_roi_collection PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/classification
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_roi_collection PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_roi_collection)

# Phase 10A Task 10.1 — ROI shapefile + sidecar JSON I/O tests
add_executable(test_roi_io test_roi_io.cpp)
target_link_libraries(test_roi_io PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Test
  qgis_core
  qgis_analysis
  GDAL::GDAL
)
target_include_directories(test_roi_io PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/classification
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_roi_io PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_roi_io)

# Band ratio and IHS transform tests
add_executable(test_band_ratio test_band_ratio.cpp)
target_link_libraries(test_band_ratio PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
  sicnu_data
)
target_include_directories(test_band_ratio PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_band_ratio)

# Phase 10A Task 10.2 — Classification main window shell tests.
# Only built when OpenCV ml component is available (qgis_app_classify exists).
if(TARGET qgis_app_classify)
  add_executable(test_classification_task_center
    test_classification_task_center.cpp
    ${CMAKE_SOURCE_DIR}/src/app/shell/rs_job_runner.cpp
    ${CMAKE_SOURCE_DIR}/src/app/shell/gui_job_adapter.cpp
    ${CMAKE_SOURCE_DIR}/src/app/dialogs/dialog_utils.cpp
  )
  target_link_libraries(test_classification_task_center PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    qgis_core
    qgis_analysis
    qgis_app_classify
    Sicnu::session_map
    sicnu_jobs
    sicnu_task_center
    GDAL::GDAL
    ${OpenCV_LIBRARIES}
  sicnu_data
  )
  target_include_directories(test_classification_task_center PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/analysis
    ${CMAKE_SOURCE_DIR}/src/analysis/classification
    ${CMAKE_SOURCE_DIR}/src/app/classification
    ${OpenCV_INCLUDE_DIRS}
    ${CMAKE_BINARY_DIR}
  )
  set_target_properties(test_classification_task_center PROPERTIES AUTOMOC ON)
  sicnu_discover_tests(test_classification_task_center)

  # Shell QObject TUs live on sicnu_geo_rs only (not qgis_app_classify) to avoid
  # MOC ODR. Full-window classify tests must compile the same shell sources here.
  # Follow-up: extract a shared OBJECT/static lib (e.g. sicnu_app_shell) once more
  # tests need this list — do not add these .cpp files to qgis_app_classify.
  add_executable(test_classification_window
    test_classification_window.cpp
    ${CMAKE_SOURCE_DIR}/src/app/shell/rs_job_runner.cpp
    ${CMAKE_SOURCE_DIR}/src/app/shell/gui_job_adapter.cpp
    ${CMAKE_SOURCE_DIR}/src/app/dialogs/dialog_utils.cpp
  )
  target_link_libraries(test_classification_window PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Test
    qgis_core
    qgis_gui
    qgis_analysis
    qgis_app_classify
    Sicnu::session_map
    sicnu_jobs
    sicnu_task_center
  sicnu_data
  )
  target_include_directories(test_classification_window PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/analysis
    ${CMAKE_SOURCE_DIR}/src/analysis/classification
    ${CMAKE_SOURCE_DIR}/src/app
    ${CMAKE_SOURCE_DIR}/src/app/classification
    ${CMAKE_BINARY_DIR}
  )
  set_target_properties(test_classification_window PROPERTIES AUTOMOC ON)
  target_compile_definitions(test_classification_window PRIVATE
    SICNU_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
  )
  sicnu_discover_tests(test_classification_window)

  # Phase 10A Task 10.3 — Class table widget tests.
  add_executable(test_class_table_widget test_class_table_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/app/shell/gui_job_adapter.cpp)
  target_link_libraries(test_class_table_widget PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Test
    qgis_core
    qgis_analysis
    qgis_app_classify
  )
  target_include_directories(test_class_table_widget PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/analysis
    ${CMAKE_SOURCE_DIR}/src/analysis/classification
    ${CMAKE_SOURCE_DIR}/src/app/classification
    ${CMAKE_BINARY_DIR}
  )
  set_target_properties(test_class_table_widget PROPERTIES AUTOMOC ON)
  sicnu_discover_tests(test_class_table_widget)

  # Global help/tooltip/confirmation sweep — catalog entries + dialog help buttons.
  add_executable(test_global_help_tips test_global_help_tips.cpp
    ${CMAKE_SOURCE_DIR}/src/app/dialogs/dialog_help_catalog.cpp
    ${CMAKE_SOURCE_DIR}/src/app/shell/gui_job_adapter.cpp)
  target_link_libraries(test_global_help_tips PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Test
    qgis_core
    qgis_analysis
    qgis_app_classify
  )
  target_include_directories(test_global_help_tips PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/app
    ${CMAKE_SOURCE_DIR}/src/app/classification
    ${CMAKE_SOURCE_DIR}/src/analysis
    ${CMAKE_SOURCE_DIR}/src/analysis/classification
    ${CMAKE_BINARY_DIR}
  )
  set_target_properties(test_global_help_tips PROPERTIES AUTOMOC ON)
  sicnu_discover_tests(test_global_help_tips)

  # RS Studio Help System & User Guide verification suite
  add_executable(test_help_system test_help_system.cpp
    ${CMAKE_SOURCE_DIR}/src/app/dialogs/dialog_utils.cpp
    ${CMAKE_SOURCE_DIR}/src/app/dialogs/dialog_help_catalog.cpp
    ${CMAKE_SOURCE_DIR}/src/app/dialogs/help_viewer_dialog.cpp)
  target_link_libraries(test_help_system PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Test
    sicnu_processing
    sicnu_data
    qgis_core
    qgis_gui
  )
  target_include_directories(test_help_system PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/app
    ${CMAKE_BINARY_DIR}
  )
  set_target_properties(test_help_system PROPERTIES AUTOMOC ON)
  sicnu_discover_tests(test_help_system)

  # Layout Designer & Tools verification suite
  add_executable(test_layout_designer test_layout_designer.cpp
    ${CMAKE_SOURCE_DIR}/src/app/layout/qgslayoutdesignerdialog.cpp)
  target_link_libraries(test_layout_designer PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Test
    sicnu_processing
    sicnu_agent
    qgis_core
    qgis_gui
  )
  target_include_directories(test_layout_designer PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/app
    ${CMAKE_BINARY_DIR}
  )
  set_target_properties(test_layout_designer PROPERTIES AUTOMOC ON)
  sicnu_discover_tests(test_layout_designer)

# Headless MCP cartographic layout tools (Cartographic Layout Studio).
sicnu_add_test(test_layout_tools)

  # Phase 10A Task 10.4 — Pixel rasterizer (geometry -> pixel index set)
  add_executable(test_pixel_rasterizer test_pixel_rasterizer.cpp)
  target_link_libraries(test_pixel_rasterizer PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    qgis_core
    qgis_analysis
    GDAL::GDAL
  )
  target_include_directories(test_pixel_rasterizer PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/analysis
    ${CMAKE_SOURCE_DIR}/src/analysis/classification
    ${CMAKE_BINARY_DIR}
  )
  sicnu_discover_tests(test_pixel_rasterizer)

  # Phase 10A Task 10.4 — ROI polygon map tool (3 clicks + dblclk -> emit)
  add_executable(test_roi_tool_polygon test_roi_tool_polygon.cpp
    ${CMAKE_SOURCE_DIR}/src/app/shell/gui_job_adapter.cpp)
  target_link_libraries(test_roi_tool_polygon PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Test
    qgis_core
    qgis_gui
    qgis_analysis
    qgis_app_classify
  )
  target_include_directories(test_roi_tool_polygon PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/analysis
    ${CMAKE_SOURCE_DIR}/src/analysis/classification
    ${CMAKE_SOURCE_DIR}/src/app/classification
    ${CMAKE_BINARY_DIR}
  )
  set_target_properties(test_roi_tool_polygon PROPERTIES AUTOMOC ON)
  sicnu_discover_tests(test_roi_tool_polygon)

  # Magic-wand ROI tool: roiDrawn geometry must reproduce the flooded pixels (#283).
  add_executable(test_roi_tool_magicwand test_roi_tool_magicwand.cpp
    ${CMAKE_SOURCE_DIR}/src/app/shell/gui_job_adapter.cpp)
  target_link_libraries(test_roi_tool_magicwand PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Test
    qgis_core
    qgis_gui
    qgis_analysis
    qgis_app_classify
  )
  target_include_directories(test_roi_tool_magicwand PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/analysis
    ${CMAKE_SOURCE_DIR}/src/analysis/classification
    ${CMAKE_SOURCE_DIR}/src/app/classification
    ${CMAKE_BINARY_DIR}
  )
  set_target_properties(test_roi_tool_magicwand PROPERTIES AUTOMOC ON)
  sicnu_discover_tests(test_roi_tool_magicwand)

  # Phase 10A Task 10.5 — Spectral curve widget tests.
  add_executable(test_spectral_curve test_spectral_curve.cpp
    ${CMAKE_SOURCE_DIR}/src/app/shell/gui_job_adapter.cpp)
  target_link_libraries(test_spectral_curve PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    qgis_core
    qgis_analysis
    qgis_app_classify
  )
  target_include_directories(test_spectral_curve PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/app/classification
    ${CMAKE_BINARY_DIR}
  )
  set_target_properties(test_spectral_curve PROPERTIES AUTOMOC ON)
  sicnu_discover_tests(test_spectral_curve)

  # Phase 10A Task 10.6 — Jeffries-Matusita separability tests.
  add_executable(test_jm_separability test_jm_separability.cpp)
  target_link_libraries(test_jm_separability PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    Qt6::Gui
    qgis_core
    qgis_analysis
    GDAL::GDAL
    ${OpenCV_LIBS}
  )
  target_include_directories(test_jm_separability PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/analysis
    ${CMAKE_SOURCE_DIR}/src/analysis/classification
    ${OpenCV_INCLUDE_DIRS}
    ${CMAKE_BINARY_DIR}
  )
  sicnu_discover_tests(test_jm_separability)

  # Phase 10A Task 10.7 — Magic-wand flood-fill algorithm tests.
  add_executable(test_flood_fill test_flood_fill.cpp)
  target_link_libraries(test_flood_fill PRIVATE
    Catch2::Catch2WithMain
    qgis_core
    qgis_analysis
    ${OpenCV_LIBS}
  )
  target_include_directories(test_flood_fill PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/analysis
    ${CMAKE_SOURCE_DIR}/src/analysis/classification
    ${OpenCV_INCLUDE_DIRS}
    ${CMAKE_BINARY_DIR}
  )
  sicnu_discover_tests(test_flood_fill)

  # Phase 10A Task 10.8 — NormalBayes backend accuracy test.
  add_executable(test_classifier_normalbayes test_classifier_normalbayes.cpp)
  target_link_libraries(test_classifier_normalbayes PRIVATE
    Catch2::Catch2WithMain
    qgis_core
    qgis_analysis
    ${OpenCV_LIBS}
  )
  target_include_directories(test_classifier_normalbayes PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/analysis
    ${CMAKE_SOURCE_DIR}/src/analysis/classification
    ${OpenCV_INCLUDE_DIRS}
    ${CMAKE_BINARY_DIR}
  )
  sicnu_discover_tests(test_classifier_normalbayes)

  # Phase 10A Task 10.8 — SVM (RBF) backend accuracy test.
  add_executable(test_classifier_svm test_classifier_svm.cpp)
  target_link_libraries(test_classifier_svm PRIVATE
    Catch2::Catch2WithMain
    qgis_core
    qgis_analysis
    ${OpenCV_LIBS}
  )
  target_include_directories(test_classifier_svm PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/analysis
    ${CMAKE_SOURCE_DIR}/src/analysis/classification
    ${OpenCV_INCLUDE_DIRS}
    ${CMAKE_BINARY_DIR}
  )
  sicnu_discover_tests(test_classifier_svm)

  # Foundation 5.0 Milestone H — kNN + statistical backends + factory mapping.
  add_executable(test_classifier_foundation5 test_classifier_foundation5.cpp)
  target_link_libraries(test_classifier_foundation5 PRIVATE
    Catch2::Catch2WithMain
    qgis_core
    qgis_analysis
    ${OpenCV_LIBS}
  )
  target_include_directories(test_classifier_foundation5 PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/analysis
    ${CMAKE_SOURCE_DIR}/src/analysis/classification
    ${OpenCV_INCLUDE_DIRS}
    ${CMAKE_BINARY_DIR}
  )
  sicnu_discover_tests(test_classifier_foundation5)

  # Phase 10A Task 10.8 — KMeans backend cluster-centre test.
  add_executable(test_classifier_kmeans test_classifier_kmeans.cpp)
  target_link_libraries(test_classifier_kmeans PRIVATE
    Catch2::Catch2WithMain
    qgis_core
    qgis_analysis
    ${OpenCV_LIBS}
  )
  target_include_directories(test_classifier_kmeans PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/analysis
    ${CMAKE_SOURCE_DIR}/src/analysis/classification
    ${OpenCV_INCLUDE_DIRS}
    ${CMAKE_BINARY_DIR}
  )

  # Scientific Algorithms 7.0 — ISODATA backend tests.
  add_executable(test_classifier_isodata test_classifier_isodata.cpp)
  target_link_libraries(test_classifier_isodata PRIVATE
    Catch2::Catch2WithMain
    qgis_core
    qgis_analysis
    ${OpenCV_LIBS}
  )
  target_include_directories(test_classifier_isodata PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/analysis
    ${CMAKE_SOURCE_DIR}/src/analysis/classification
    ${OpenCV_INCLUDE_DIRS}
    ${CMAKE_BINARY_DIR}
  )
  sicnu_discover_tests(test_classifier_kmeans)
  sicnu_discover_tests(test_classifier_isodata)

  # OBIA Classification Optimization — RandomForest backend test.
  add_executable(test_classifier_random_forest test_classifier_random_forest.cpp)
  target_link_libraries(test_classifier_random_forest PRIVATE
    Catch2::Catch2WithMain
    qgis_core
    qgis_analysis
    ${OpenCV_LIBS}
  )
  target_include_directories(test_classifier_random_forest PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/analysis
    ${CMAKE_SOURCE_DIR}/src/analysis/classification
    ${OpenCV_INCLUDE_DIRS}
    ${CMAKE_BINARY_DIR}
  )
  sicnu_discover_tests(test_classifier_random_forest)

  # OBIA Classification Optimization — MLP classifier test.
  add_executable(test_classifier_mlp test_classifier_mlp.cpp)
  target_link_libraries(test_classifier_mlp PRIVATE
    Catch2::Catch2WithMain
    qgis_core
    qgis_analysis
    ${OpenCV_LIBS}
  )
  target_include_directories(test_classifier_mlp PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/analysis
    ${CMAKE_SOURCE_DIR}/src/analysis/classification
    ${OpenCV_INCLUDE_DIRS}
    ${CMAKE_BINARY_DIR}
  )
  sicnu_discover_tests(test_classifier_mlp)

  # OBIA Classification Optimization — MinMax feature scaler test.
  add_executable(test_feature_scaler_minmax test_feature_scaler_minmax.cpp)
  target_link_libraries(test_feature_scaler_minmax PRIVATE
    Catch2::Catch2WithMain
    qgis_core
    qgis_analysis
    ${OpenCV_LIBS}
  )
  target_include_directories(test_feature_scaler_minmax PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/analysis
    ${CMAKE_SOURCE_DIR}/src/analysis/classification
    ${OpenCV_INCLUDE_DIRS}
    ${CMAKE_BINARY_DIR}
  )
  sicnu_discover_tests(test_feature_scaler_minmax)

  # OBIA Classification Optimization — Hierarchy class consolidator test.
  add_executable(test_hierarchy_class_consolidator test_hierarchy_class_consolidator.cpp)
  target_link_libraries(test_hierarchy_class_consolidator PRIVATE
    Catch2::Catch2WithMain
    qgis_core
    qgis_analysis
    ${OpenCV_LIBS}
  )
  target_include_directories(test_hierarchy_class_consolidator PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/analysis
    ${CMAKE_SOURCE_DIR}/src/analysis/segmentation
    ${OpenCV_INCLUDE_DIRS}
    ${CMAKE_BINARY_DIR}
  )
  sicnu_discover_tests(test_hierarchy_class_consolidator)

  # OBIA Classification Optimization — Feature selection tree test.
  add_executable(test_feature_selection test_feature_selection.cpp)
  target_link_libraries(test_feature_selection PRIVATE
    Catch2::Catch2WithMain
    qgis_core
    qgis_analysis
    ${OpenCV_LIBS}
  )
  target_include_directories(test_feature_selection PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/analysis
    ${CMAKE_SOURCE_DIR}/src/analysis/segmentation
    ${OpenCV_INCLUDE_DIRS}
    ${CMAKE_BINARY_DIR}
  )
  sicnu_discover_tests(test_feature_selection)

  # Phase 10A review patch — stratified train/test split helper test.
  add_executable(test_stratified_split test_stratified_split.cpp)
  target_link_libraries(test_stratified_split PRIVATE
    Catch2::Catch2WithMain
    qgis_core
    qgis_analysis
    ${OpenCV_LIBS}
  )
  target_include_directories(test_stratified_split PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/analysis
    ${CMAKE_SOURCE_DIR}/src/analysis/classification
    ${OpenCV_INCLUDE_DIRS}
    ${CMAKE_BINARY_DIR}
  )
  sicnu_discover_tests(test_stratified_split)

  # Classification v1.1 Task 1 — feature scaler (z-score + JSON).
  add_executable(test_feature_scaler test_feature_scaler.cpp)
  target_link_libraries(test_feature_scaler PRIVATE
    Catch2::Catch2WithMain
    qgis_analysis
    Qt6::Core
    ${OpenCV_LIBS}
  )
  target_include_directories(test_feature_scaler PRIVATE
    ${CMAKE_SOURCE_DIR}/src/analysis/classification
    ${CMAKE_BINARY_DIR}
  )
  sicnu_discover_tests(test_feature_scaler)

  # Classification v1.1 — viewport map extent → pixel window.
  add_executable(test_pixel_window test_pixel_window.cpp)
  target_link_libraries(test_pixel_window PRIVATE
    Catch2::Catch2WithMain
    qgis_core
    qgis_analysis
    GDAL::GDAL
  )
  target_include_directories(test_pixel_window PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/analysis
    ${CMAKE_SOURCE_DIR}/src/analysis/classification
    ${CMAKE_BINARY_DIR}
  )
  sicnu_discover_tests(test_pixel_window)

  # Classification edge handling — NoData + user ignore values.
  add_executable(test_pixel_ignore_options test_pixel_ignore_options.cpp)
  target_link_libraries(test_pixel_ignore_options PRIVATE
    Catch2::Catch2WithMain
    qgis_analysis
    Qt6::Core
  )
  target_include_directories(test_pixel_ignore_options PRIVATE
    ${CMAKE_SOURCE_DIR}/src/analysis/classification
    ${CMAKE_BINARY_DIR}
  )
  sicnu_discover_tests(test_pixel_ignore_options)

  # Classification workflow — .rscproj JSON project persistence.
  add_executable(test_classification_project test_classification_project.cpp)
  target_link_libraries(test_classification_project PRIVATE
    Catch2::Catch2WithMain
    qgis_analysis
    Qt6::Core
  sicnu_data
  )
  target_include_directories(test_classification_project PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/analysis
    ${CMAKE_SOURCE_DIR}/src/analysis/classification
    ${CMAKE_BINARY_DIR}
  )
  sicnu_discover_tests(test_classification_project)

  # Classification v1.1 — dirty flag + workflow QSettings session.
  add_executable(test_classify_session_state
    test_classify_session_state.cpp
    ${CMAKE_SOURCE_DIR}/src/app/classification/rs_classify_session_state.cpp
  )
  target_link_libraries(test_classify_session_state PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
  sicnu_data
  )
  target_include_directories(test_classify_session_state PRIVATE
    ${CMAKE_SOURCE_DIR}/src/app/classification
  )
  sicnu_discover_tests(test_classify_session_state)

  # Classification workflow — soft gates + step completion.
  add_executable(test_classify_workflow_controller test_classify_workflow_controller.cpp
    # SicnuUi chrome symbols pulled transitively via qgis_app_classify moc unit.
    ${CMAKE_SOURCE_DIR}/src/app/dialogs/dialog_utils.cpp
    ${CMAKE_SOURCE_DIR}/src/app/shell/gui_job_adapter.cpp
  )
  target_link_libraries(test_classify_workflow_controller PRIVATE
    Catch2::Catch2WithMain
    qgis_app_classify
    Qt6::Core
    Qt6::Widgets
  sicnu_data
  )
  target_include_directories(test_classify_workflow_controller PRIVATE
    ${CMAKE_SOURCE_DIR}/src/app/classification
    ${CMAKE_SOURCE_DIR}/src/app
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
  )
  set_target_properties(test_classify_workflow_controller PROPERTIES AUTOMOC ON)
  sicnu_discover_tests(test_classify_workflow_controller)

  # Phase 10A Task 10.9 — confusion matrix + Kappa accuracy assessment.
  add_executable(test_accuracy_assessment test_accuracy_assessment.cpp)
  target_link_libraries(test_accuracy_assessment PRIVATE
    Catch2::Catch2WithMain
    qgis_core
    qgis_analysis
    ${OpenCV_LIBS}
  )
  target_include_directories(test_accuracy_assessment PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/analysis
    ${CMAKE_SOURCE_DIR}/src/analysis/classification
    ${OpenCV_INCLUDE_DIRS}
    ${CMAKE_BINARY_DIR}
  )
  sicnu_discover_tests(test_accuracy_assessment)

  # Classification accuracy panel wiring tests (#326)
  add_executable(test_accuracy_panel_wiring test_accuracy_panel_wiring.cpp
    ${CMAKE_SOURCE_DIR}/src/app/classification/rs_accuracy_panel.cpp
    ${CMAKE_SOURCE_DIR}/src/app/classification/rs_classify_workflow_controller.cpp
    ${CMAKE_SOURCE_DIR}/src/app/dialogs/dialog_help_catalog.cpp
  )
  target_link_libraries(test_accuracy_panel_wiring PRIVATE
    Catch2::Catch2
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    qgis_core
    qgis_gui
    qgis_analysis
    ${OpenCV_LIBS}
  )
  target_include_directories(test_accuracy_panel_wiring PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/app
    ${CMAKE_SOURCE_DIR}/src/app/classification
    ${CMAKE_SOURCE_DIR}/src/analysis
    ${CMAKE_SOURCE_DIR}/src/analysis/classification
    ${OpenCV_INCLUDE_DIRS}
    ${CMAKE_BINARY_DIR}
  )
  sicnu_discover_tests(test_accuracy_panel_wiring)

  # Classification workflow — post-process pure operators (sieve/majority/clump/recode).
  add_executable(test_post_process test_post_process.cpp)
  target_link_libraries(test_post_process PRIVATE
    Catch2::Catch2WithMain
    qgis_core
    qgis_analysis
    sicnu_operators
    Qt6::Core
    Qt6::Gui
    GDAL::GDAL
    ${OpenCV_LIBS}
    ${JSONCPP_LIBRARIES}
  )
  target_include_directories(test_post_process PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/analysis
    ${CMAKE_SOURCE_DIR}/src/analysis/classification
    ${OpenCV_INCLUDE_DIRS}
    ${CMAKE_BINARY_DIR}
  )
  sicnu_discover_tests(test_post_process)

  # Phase 10A.1.1 — Hungarian min-cost assignment (Munkres O(n^3)).
  add_executable(test_hungarian_assignment test_hungarian_assignment.cpp)
  target_link_libraries(test_hungarian_assignment PRIVATE
    Catch2::Catch2WithMain
    qgis_core
    qgis_analysis
    ${OpenCV_LIBS}
  )
  target_include_directories(test_hungarian_assignment PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/analysis
    ${CMAKE_SOURCE_DIR}/src/analysis/classification
    ${OpenCV_INCLUDE_DIRS}
    ${CMAKE_BINARY_DIR}
  )
  sicnu_discover_tests(test_hungarian_assignment)

  # Phase 10A.1.2 — Stratified 5-fold cross validation.
  add_executable(test_cross_validation test_cross_validation.cpp)
  target_link_libraries(test_cross_validation PRIVATE
    Catch2::Catch2WithMain
    qgis_core
    qgis_analysis
    ${OpenCV_LIBS}
  )
  target_include_directories(test_cross_validation PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/analysis
    ${CMAKE_SOURCE_DIR}/src/analysis/classification
    ${OpenCV_INCLUDE_DIRS}
    ${CMAKE_BINARY_DIR}
  )
  sicnu_discover_tests(test_cross_validation)

  # Phase 10A.1.3 — Classifier isFitted state + save/load round-trip.
  add_executable(test_classifier_load_save test_classifier_load_save.cpp)
  target_link_libraries(test_classifier_load_save PRIVATE
    Catch2::Catch2WithMain
    qgis_core
    qgis_analysis
    Qt6::Core
    ${OpenCV_LIBS}
  )
  target_include_directories(test_classifier_load_save PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/analysis
    ${CMAKE_SOURCE_DIR}/src/analysis/classification
    ${OpenCV_INCLUDE_DIRS}
    ${CMAKE_BINARY_DIR}
  )
  sicnu_discover_tests(test_classifier_load_save)

  # Phase 10A Task 10.8 — End-to-end classification task test.
  add_executable(test_classification_e2e test_classification_e2e.cpp
    ${CMAKE_SOURCE_DIR}/src/app/shell/gui_job_adapter.cpp)
  target_link_libraries(test_classification_e2e PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    Qt6::Gui
    qgis_core
    qgis_analysis
    qgis_app_classify
    GDAL::GDAL
    ${OpenCV_LIBS}
  )
  target_include_directories(test_classification_e2e PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/analysis
    ${CMAKE_SOURCE_DIR}/src/analysis/classification
    ${CMAKE_SOURCE_DIR}/src/app/classification
    ${OpenCV_INCLUDE_DIRS}
    ${CMAKE_BINARY_DIR}
  )
  set_target_properties(test_classification_e2e PROPERTIES AUTOMOC ON)
  sicnu_discover_tests(test_classification_e2e)

  # ADR 0019 S1 — unified training-sample extraction module tests.
  add_executable(test_training_data_extraction test_training_data_extraction.cpp)
  target_link_libraries(test_training_data_extraction PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    Qt6::Gui
    qgis_core
    qgis_analysis
    GDAL::GDAL
    ${OpenCV_LIBS}
  )
  target_include_directories(test_training_data_extraction PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/analysis
    ${CMAKE_SOURCE_DIR}/src/analysis/classification
    ${OpenCV_INCLUDE_DIRS}
    ${CMAKE_BINARY_DIR}
  )
  sicnu_discover_tests(test_training_data_extraction)

  # ADR 0019 S2 — classification pipeline module tests (seam-level, no QgsTask).
  add_executable(test_classification_pipeline test_classification_pipeline.cpp)
  target_link_libraries(test_classification_pipeline PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    Qt6::Gui
    qgis_core
    qgis_analysis
    GDAL::GDAL
    ${OpenCV_LIBS}
  )
  target_include_directories(test_classification_pipeline PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/analysis
    ${CMAKE_SOURCE_DIR}/src/analysis/classification
    ${OpenCV_INCLUDE_DIRS}
    ${CMAKE_BINARY_DIR}
  )
  sicnu_discover_tests(test_classification_pipeline)
endif()

# Phase 10B.1 — Segment data model + feature extraction tests
add_executable(test_segment_features test_segment_features.cpp)
target_link_libraries(test_segment_features PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  qgis_core
  qgis_analysis
  GDAL::GDAL
)
target_include_directories(test_segment_features PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
if(SICNU_HAS_OPENCV)
  target_link_libraries(test_segment_features PRIVATE ${OpenCV_LIBS})
  target_include_directories(test_segment_features PRIVATE ${OpenCV_INCLUDE_DIRS})
endif()
sicnu_discover_tests(test_segment_features)

# Hierarchical OBIA V1 — parent-link, hierarchy, F2a, writeback (#16–#21)
add_executable(test_object_hierarchy test_object_hierarchy.cpp)
target_link_libraries(test_object_hierarchy PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  qgis_core
  qgis_analysis
  GDAL::GDAL
)
target_include_directories(test_object_hierarchy PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/segmentation
  ${CMAKE_SOURCE_DIR}/src/analysis/classification
  ${CMAKE_BINARY_DIR}
)
if(SICNU_HAS_OPENCV)
  target_link_libraries(test_object_hierarchy PRIVATE ${OpenCV_LIBS})
  target_include_directories(test_object_hierarchy PRIVATE ${OpenCV_INCLUDE_DIRS})
endif()
sicnu_discover_tests(test_object_hierarchy)

# Phase 10B.4 — OBIA classification task tests (OpenCV required)
if(SICNU_HAS_OPENCV AND TARGET qgis_app_obia)
  # #30 — OBIA flat segmentation Task Center seam
  add_executable(test_obia_task_center test_obia_task_center.cpp)
  target_link_libraries(test_obia_task_center PRIVATE
    sicnu_operators
    Catch2::Catch2WithMain
    Qt6::Core
    Qt6::Gui
    qgis_core
    qgis_analysis
    qgis_app_obia
    sicnu_jobs
    sicnu_task_center
    GDAL::GDAL
    ${OpenCV_LIBRARIES}
  )
  target_include_directories(test_obia_task_center PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/app
    ${CMAKE_SOURCE_DIR}/src/app/obia
    ${CMAKE_SOURCE_DIR}/src/analysis
    ${CMAKE_SOURCE_DIR}/src/analysis/segmentation
    ${OpenCV_INCLUDE_DIRS}
    ${CMAKE_BINARY_DIR}
  )
  set_target_properties(test_obia_task_center PROPERTIES AUTOMOC ON)
  sicnu_discover_tests(test_obia_task_center)

  # #663 — rs:obia_* operator contract tests (engine matrix, CSV interchange,
  # interactive classify, hierarchy rehydrate).
  add_executable(test_obia_operators test_obia_operators.cpp)
  target_link_libraries(test_obia_operators PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    Qt6::Gui
    qgis_core
    qgis_analysis
    qgis_app_obia
    sicnu_operators
    sicnu_task_center
    GDAL::GDAL
    ${OpenCV_LIBRARIES}
  )
  target_include_directories(test_obia_operators PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/analysis
    ${CMAKE_SOURCE_DIR}/src/analysis/segmentation
    ${CMAKE_SOURCE_DIR}/src/analysis/classification
    ${OpenCV_INCLUDE_DIRS}
    ${CMAKE_BINARY_DIR}
  )
  sicnu_discover_tests(test_obia_operators)

  # #663 architectural guardrail: OBIA GUI stays an operator client.
  add_executable(test_obia_architecture test_obia_architecture.cpp)
  target_link_libraries(test_obia_architecture PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    Qt6::Gui
  )
  target_compile_definitions(test_obia_architecture PRIVATE
    CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
  )
  sicnu_discover_tests(test_obia_architecture)

  # Phase 10B.6 — OBIA end-to-end integration tests
  add_executable(test_obia_integration
    test_obia_integration.cpp
    ${CMAKE_SOURCE_DIR}/src/app/shell/gui_job_adapter.cpp
  )
  target_link_libraries(test_obia_integration PRIVATE
    sicnu_operators
    Catch2::Catch2WithMain
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    qgis_core
    qgis_gui
    qgis_analysis
    qgis_app_obia
    GDAL::GDAL
    ${OpenCV_LIBS}
  )
  target_include_directories(test_obia_integration PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/analysis
    ${CMAKE_SOURCE_DIR}/src/analysis/classification
    ${CMAKE_SOURCE_DIR}/src/analysis/segmentation
    ${CMAKE_SOURCE_DIR}/src/app/obia
    ${OpenCV_INCLUDE_DIRS}
    ${CMAKE_BINARY_DIR}
  )
  set_target_properties(test_obia_integration PROPERTIES AUTOMOC ON)
  sicnu_discover_tests(test_obia_integration)

  # Phase 10B.5 — OBIA main window UI smoke tests
  add_executable(test_obia_main_window
    test_obia_main_window.cpp
    ${CMAKE_SOURCE_DIR}/src/app/shell/gui_job_adapter.cpp
  )
  target_link_libraries(test_obia_main_window PRIVATE
    sicnu_operators
    Catch2::Catch2WithMain
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    qgis_core
    qgis_gui
    qgis_analysis
    qgis_app_obia
    sicnu_jobs
    sicnu_task_center
    # qgis_app_classify (pulled in via qgis_app_obia) references
    # WorkflowRuntime (rs_classify_workflow_bridge); resolve it here.
    sicnu_workflow
    sicnu_workflow_runtime
    sicnu_agent
    GDAL::GDAL
    ${OpenCV_LIBRARIES}
  )
  target_include_directories(test_obia_main_window PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/app
    ${CMAKE_SOURCE_DIR}/src/app/obia
    ${OpenCV_INCLUDE_DIRS}
    ${CMAKE_BINARY_DIR}
  )
  set_target_properties(test_obia_main_window PROPERTIES AUTOMOC ON)
  sicnu_discover_tests(test_obia_main_window)

  add_executable(test_obia_segmentation test_obia_segmentation.cpp)
  target_link_libraries(test_obia_segmentation PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    Qt6::Gui
    qgis_core
    qgis_analysis
    qgis_app_obia
    GDAL::GDAL
    ${OpenCV_LIBS}
  )
  target_include_directories(test_obia_segmentation PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/app/obia
    ${OpenCV_INCLUDE_DIRS}
    ${CMAKE_BINARY_DIR}
  )
  sicnu_discover_tests(test_obia_segmentation)
endif()

# ---------------------------------------------------------------------------
# Orphan tests: pipeline runner + RS/OpenCV operators (link only when deps exist)
# ---------------------------------------------------------------------------
if(TARGET sicnu_operators)
  # Pipeline runner (CLI) — needs runner sources + operators + jsoncpp
  add_executable(test_pipeline_runner
    test_pipeline_runner.cpp
    ${CMAKE_SOURCE_DIR}/src/cli/rs_pipeline_runner.cpp
  )
  target_link_libraries(test_pipeline_runner PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    sicnu_operators
    sicnu_processing
    sicnu_task_center
    sicnu_jobs
    sicnu_workflow
    sicnu_workflow_runtime
    sicnu_python_isolated
    Sicnu::qgis_display
    Sicnu::data
    qgis_core
    qgis_gui
    GDAL::GDAL
  )
  target_include_directories(test_pipeline_runner PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
  )
  target_compile_definitions(test_pipeline_runner PRIVATE
    TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/tests/data"
  )
  # Headless plugin hosting (ADR 0023): the runner composes PluginHost with a
  # view-less ProjectContext and a widget-free SicnuAppInterface — compiled in
  # only when embedded-Python support is enabled (mirrors plugin_host.cpp).
  if(SICNU_EMBED_PYTHON)
    target_sources(test_pipeline_runner PRIVATE
      ${CMAKE_SOURCE_DIR}/src/core/plugin_host.cpp
      ${CMAKE_SOURCE_DIR}/src/app/python/sicnu_app_interface.cpp
    )
    target_include_directories(test_pipeline_runner PRIVATE
      ${CMAKE_SOURCE_DIR}/src/core
      ${CMAKE_SOURCE_DIR}/src/app
    )
    target_compile_definitions(test_pipeline_runner PRIVATE SICNU_EMBED_PYTHON=1)
  endif()
  sicnu_link_jsoncpp(test_pipeline_runner)
  sicnu_discover_tests(test_pipeline_runner)

  # RSOperator framework unit tests (no OpenCV required)
  add_executable(test_rs_operator test_rs_operator.cpp)
  target_link_libraries(test_rs_operator PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    sicnu_operators
  )
  target_include_directories(test_rs_operator PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
  )
  sicnu_link_jsoncpp(test_rs_operator)
  sicnu_discover_tests(test_rs_operator)

  # Native RS operator integration tests (registry, schema, execution on
  # synthetic rasters; OpenCV-backed cases compile out without OpenCV).
  # ADR 0019 S3 — previously an orphan file registered in no target.
  add_executable(test_rs_operators test_rs_operators.cpp)
  target_link_libraries(test_rs_operators PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    qgis_core
    qgis_gui
    sicnu_processing
    sicnu_operators
    GDAL::GDAL
  )
  target_include_directories(test_rs_operators PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
  )
  sicnu_link_jsoncpp(test_rs_operators)
  sicnu_discover_tests(test_rs_operators)

  add_executable(test_adversarial_m1 test_adversarial_m1.cpp)
  target_link_libraries(test_adversarial_m1 PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    qgis_core
    qgis_gui
    sicnu_processing
    sicnu_operators
    GDAL::GDAL
  )
  target_include_directories(test_adversarial_m1 PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
  )
  sicnu_link_jsoncpp(test_adversarial_m1)
  sicnu_discover_tests(test_adversarial_m1)

  # Desktop Workbench UX 4.0 — band tool operator promotions (Milestone C)
  add_executable(test_rs_band_tools_operators test_rs_band_tools_operators.cpp)
  target_link_libraries(test_rs_band_tools_operators PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    qgis_core
    qgis_gui
    sicnu_processing
    sicnu_operators
    GDAL::GDAL
  )
  target_include_directories(test_rs_band_tools_operators PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
  )
  sicnu_link_jsoncpp(test_rs_band_tools_operators)
  sicnu_discover_tests(test_rs_band_tools_operators)

  # OTB operator tests
  add_executable(test_otb_operators test_otb_operators.cpp)
  target_link_libraries(test_otb_operators PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    qgis_core
    qgis_gui
    sicnu_processing
    sicnu_operators
    GDAL::GDAL
  )
  target_include_directories(test_otb_operators PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
  )
  sicnu_link_jsoncpp(test_otb_operators)
  sicnu_discover_tests(test_otb_operators)

  # RS Operation Logger tests
  add_executable(test_operation_logger test_operation_logger.cpp)
  target_link_libraries(test_operation_logger PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    qgis_core
    qgis_gui
    sicnu_processing
    sicnu_operators
    GDAL::GDAL
  )
  target_include_directories(test_operation_logger PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
  )
  sicnu_link_jsoncpp(test_operation_logger)
  sicnu_discover_tests(test_operation_logger)

  # GDAL operator tests (orthorectification / reproject / clip / polygonize).
  # ADR 0091 — previously an orphan file registered in no target; the reproject
  # `reference` grid-alignment path and the ortho/GCP contracts need coverage.
  add_executable(test_gdal_ortho_operators test_gdal_ortho_operators.cpp)
  target_link_libraries(test_gdal_ortho_operators PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    qgis_core
    qgis_gui
    sicnu_processing
    sicnu_operators
    GDAL::GDAL
  )
  target_include_directories(test_gdal_ortho_operators PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
  )
  sicnu_link_jsoncpp(test_gdal_ortho_operators)
  sicnu_discover_tests(test_gdal_ortho_operators)

  # QA mask operator + kernel tests (Landsat QA_PIXEL / Sentinel-2 SCL /
  # generic bitmask; role-resolved QA band).
  add_executable(test_qa_mask test_qa_mask.cpp)
  target_link_libraries(test_qa_mask PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    qgis_core
    qgis_gui
    sicnu_processing
    sicnu_operators
    GDAL::GDAL
  )
  target_include_directories(test_qa_mask PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
  )
  sicnu_link_jsoncpp(test_qa_mask)
  sicnu_discover_tests(test_qa_mask)

  # Temporal (multi-date) remote sensing: core semantics, scientific operator
  # correctness, agent/registry integration.
  # Platform 3.0 SAR kernel numeric references (goal §6/§12).
  sicnu_add_test(test_sar_kernels)

  # Platform 3.0 SAR operator end-to-end (synthetic fixtures).
  sicnu_add_test(test_sar_operators)

  # Platform 3.0 feature cube contract.
  sicnu_add_test(test_feature_cube)

  # Platform 3.0 temporal fit numeric references (goal §7/§12).
  sicnu_add_test(test_temporal_fit)

  # Platform 3.0 model library gate: every shipped manifest parses cleanly,
  # names uniquely, and carries honest template readiness.
  sicnu_add_test(test_model_library_manifests)

  # Platform 3.0 spatiotemporal observation contracts: modality vocabulary,
  # STAC multimodal ingest, product metadata population, modality preflight.
  add_executable(test_spatiotemporal_contracts test_spatiotemporal_contracts.cpp)
  target_link_libraries(test_spatiotemporal_contracts PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    qgis_core
    Sicnu::data
    sicnu_processing
    GDAL::GDAL
  )
  target_include_directories(test_spatiotemporal_contracts PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/processing/algorithms/temporal
    ${CMAKE_BINARY_DIR}
  )
  sicnu_link_jsoncpp(test_spatiotemporal_contracts)
  sicnu_discover_tests(test_spatiotemporal_contracts)

  add_executable(test_temporal_core test_temporal_core.cpp)
  target_link_libraries(test_temporal_core PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    qgis_core
    Sicnu::data
    sicnu_processing
    GDAL::GDAL
  )
  target_include_directories(test_temporal_core PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
  )
  sicnu_link_jsoncpp(test_temporal_core)
  sicnu_discover_tests(test_temporal_core)

  add_executable(test_temporal_algorithms test_temporal_algorithms.cpp)
  target_link_libraries(test_temporal_algorithms PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    qgis_core
    Sicnu::data
    sicnu_processing
    sicnu_operators
    GDAL::GDAL
  )
  target_include_directories(test_temporal_algorithms PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
  )
  sicnu_link_jsoncpp(test_temporal_algorithms)
  sicnu_discover_tests(test_temporal_algorithms)

  # Temporal workspace: DataManager records, STAC ingestion, execution cache.
  add_executable(test_temporal_workspace test_temporal_workspace.cpp)
  target_link_libraries(test_temporal_workspace PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    Qt6::Xml
    Sicnu::data
    sicnu_processing
    GDAL::GDAL
  )
  target_include_directories(test_temporal_workspace PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/processing/algorithms/temporal
    ${CMAKE_BINARY_DIR}
  )
  sicnu_link_jsoncpp(test_temporal_workspace)
  sicnu_discover_tests(test_temporal_workspace)

  add_executable(test_temporal_agent_tools test_temporal_agent_tools.cpp)
  target_link_libraries(test_temporal_agent_tools PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    Qt6::Gui
    qgis_core
    Sicnu::data
    sicnu_processing
    sicnu_operators
    sicnu_agent
    GDAL::GDAL
  )
  target_include_directories(test_temporal_agent_tools PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
  )
  sicnu_link_jsoncpp(test_temporal_agent_tools)
  sicnu_discover_tests(test_temporal_agent_tools)

  # MNF transform tests - kernel (SNR ordering, signal recovery) + operator.
  add_executable(test_mnf test_mnf.cpp)
  target_link_libraries(test_mnf PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    qgis_core
    qgis_gui
    sicnu_processing
    sicnu_operators
    GDAL::GDAL
  )
  target_include_directories(test_mnf PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
  )
  sicnu_link_jsoncpp(test_mnf)
  sicnu_discover_tests(test_mnf)

  # Linear spectral unmixing tests - kernel (abundance recovery, error) + operator.
  add_executable(test_spectral_unmixing test_spectral_unmixing.cpp)
  target_link_libraries(test_spectral_unmixing PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    qgis_core
    qgis_gui
    sicnu_processing
    sicnu_operators
    GDAL::GDAL
  )
  target_include_directories(test_spectral_unmixing PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
  )
  sicnu_link_jsoncpp(test_spectral_unmixing)
  sicnu_discover_tests(test_spectral_unmixing)

  # RX anomaly detection tests - kernel (outlier flagging) + operator.
  add_executable(test_spectral_anomaly test_spectral_anomaly.cpp)
  target_link_libraries(test_spectral_anomaly PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    qgis_core
    qgis_gui
    sicnu_processing
    sicnu_operators
    GDAL::GDAL
  )
  target_include_directories(test_spectral_anomaly PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
  )
  sicnu_link_jsoncpp(test_spectral_anomaly)
  sicnu_discover_tests(test_spectral_anomaly)

  # Spectral resampling tests - kernel (interpolation, guards) + operator.
  add_executable(test_spectral_resampling test_spectral_resampling.cpp)
  target_link_libraries(test_spectral_resampling PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    qgis_core
    qgis_gui
    sicnu_processing
    sicnu_operators
    GDAL::GDAL
  )
  target_include_directories(test_spectral_resampling PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
  )
  sicnu_link_jsoncpp(test_spectral_resampling)
  sicnu_discover_tests(test_spectral_resampling)

  # Endmember extraction (PPI) tests - kernel (vertex recovery) + operator.
  add_executable(test_endmember_extraction test_endmember_extraction.cpp)
  target_link_libraries(test_endmember_extraction PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    qgis_core
    qgis_gui
    sicnu_processing
    sicnu_operators
    GDAL::GDAL
  )
  target_include_directories(test_endmember_extraction PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
  )
  sicnu_link_jsoncpp(test_endmember_extraction)
  sicnu_discover_tests(test_endmember_extraction)

  # Spectral library domain tests - JSON round-trip, malformed input.
  add_executable(test_spectral_library test_spectral_library.cpp)
  target_link_libraries(test_spectral_library PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    sicnu_processing
  )
  target_include_directories(test_spectral_library PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
  )
  sicnu_discover_tests(test_spectral_library)

  # ROI mean spectrum tests - mean/stddev over a polygon, wavelengths, guards.
  add_executable(test_spectral_roi test_spectral_roi.cpp)
  target_link_libraries(test_spectral_roi PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    sicnu_processing
    GDAL::GDAL
  )
  target_include_directories(test_spectral_roi PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
  )
  sicnu_discover_tests(test_spectral_roi)

  # OpenCV-backed operators (only when OpenCV operators were compiled in)
  if(SICNU_HAS_OPENCV AND OpenCV_FOUND AND OpenCV_imgproc_FOUND)
    add_executable(test_opencv_operators test_opencv_operators.cpp)
    target_link_libraries(test_opencv_operators PRIVATE
      Catch2::Catch2WithMain
      Qt6::Core
      sicnu_operators
      sicnu_processing
      GDAL::GDAL
      ${OpenCV_LIBS}
    )
    target_include_directories(test_opencv_operators PRIVATE
      ${CMAKE_SOURCE_DIR}/src
      ${CMAKE_BINARY_DIR}
      ${OpenCV_INCLUDE_DIRS}
    )
    sicnu_link_jsoncpp(test_opencv_operators)
    sicnu_discover_tests(test_opencv_operators)
  endif()
endif()

# Phase 10B.3 — Simple segmenter fallback tests
add_executable(test_simple_segmenter test_simple_segmenter.cpp)
target_link_libraries(test_simple_segmenter PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  qgis_core
  qgis_analysis
  GDAL::GDAL
)
target_include_directories(test_simple_segmenter PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_simple_segmenter)

# Phase 10B.2 — OTB segmentation parameter tests
add_executable(test_otb_segmentation_params test_otb_segmentation_params.cpp)
target_link_libraries(test_otb_segmentation_params PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  qgis_analysis
  sicnu_processing
)
target_include_directories(test_otb_segmentation_params PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_otb_segmentation_params)

# Processing Toolbox Phase 1 Task 4 — new OTB algorithm parameter tests
add_executable(test_otb_new_algorithms_params test_otb_new_algorithms_params.cpp)
target_link_libraries(test_otb_new_algorithms_params PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  qgis_analysis
  sicnu_processing
)
target_include_directories(test_otb_new_algorithms_params PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_otb_new_algorithms_params)

# Processing Toolbox Phase 1 Task 5 — OTB info upgrades + stereo rectification
add_executable(test_otb_info_upgrades test_otb_info_upgrades.cpp)
target_link_libraries(test_otb_info_upgrades PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  qgis_analysis
  sicnu_processing
)
target_include_directories(test_otb_info_upgrades PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_otb_info_upgrades)

# Comprehensive hooks test suite — 100% branch coverage for all hooks
add_executable(test_hooks_comprehensive test_hooks_comprehensive.cpp)
target_link_libraries(test_hooks_comprehensive PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
  qgis_core
  qgis_analysis
  qgis_app_georef
)
target_include_directories(test_hooks_comprehensive PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/georeferencing
  ${CMAKE_SOURCE_DIR}/src/app/georeferencer
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_hooks_comprehensive PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_hooks_comprehensive)

# Histogram Widget tests — tests the data structures, not the widget itself
# (widget is part of main app executable, not a separate library)
add_executable(test_histogram_widget test_histogram_widget.cpp)
target_link_libraries(test_histogram_widget PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
)
target_include_directories(test_histogram_widget PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_histogram_widget)

# Display stretch core — pure resolve + port apply (no GUI)
add_executable(test_display_stretch
  test_display_stretch.cpp
  ${CMAKE_SOURCE_DIR}/src/app/display/display_stretch_types.cpp
  ${CMAKE_SOURCE_DIR}/src/app/display/display_stretch_resolve.cpp
  ${CMAKE_SOURCE_DIR}/src/app/display/display_stretch_apply.cpp
)
target_link_libraries(test_display_stretch PRIVATE
  Catch2::Catch2WithMain
)
target_include_directories(test_display_stretch PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_display_stretch)

# Display stretch panel — algorithm switch / deleted-layer safety (UAF regression)
add_executable(test_histogram_stretch_widget
  test_histogram_stretch_widget.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/histogram_stretch_widget.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/histogram_widget.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/rs_scan_pool.cpp
  ${CMAKE_SOURCE_DIR}/src/app/display/display_stretch_types.cpp
  ${CMAKE_SOURCE_DIR}/src/app/display/display_stretch_resolve.cpp
  ${CMAKE_SOURCE_DIR}/src/app/display/display_stretch_apply.cpp
  ${CMAKE_SOURCE_DIR}/src/app/display/piecewise_linear_enhancement.cpp
  ${CMAKE_SOURCE_DIR}/src/app/display/qgs_display_stretch.cpp
)
target_link_libraries(test_histogram_stretch_widget PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
  qgis_core
  qgis_gui
  GDAL::GDAL
)
target_include_directories(test_histogram_stretch_widget PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_histogram_stretch_widget PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_histogram_stretch_widget)

# Guided Workflow Widget tests
add_executable(test_guided_workflow_widget test_guided_workflow_widget.cpp)
target_link_libraries(test_guided_workflow_widget PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
)
target_include_directories(test_guided_workflow_widget PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_guided_workflow_widget)

# STAC Client tests
add_executable(test_stac_client
  test_stac_client.cpp
  ${CMAKE_SOURCE_DIR}/src/app/stac_client.cpp
)
target_link_libraries(test_stac_client PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Network
)
target_include_directories(test_stac_client PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
# StacClient has Q_OBJECT — needs AUTOMOC
set_target_properties(test_stac_client PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_stac_client)

# GDAL Safe Call tests
add_executable(test_gdal_safe_call test_gdal_safe_call.cpp)
target_link_libraries(test_gdal_safe_call PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  GDAL::GDAL
)
target_include_directories(test_gdal_safe_call PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_gdal_safe_call)

# Input Validator tests
add_executable(test_input_validator test_input_validator.cpp)
target_link_libraries(test_input_validator PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  sicnu_processing
)
target_include_directories(test_input_validator PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_input_validator)

# Processing Dialogs tests
add_executable(test_processing_dialogs test_processing_dialogs.cpp)
target_link_libraries(test_processing_dialogs PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  sicnu_data
)
target_include_directories(test_processing_dialogs PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_processing_dialogs)

# Dialog Edge Cases tests
add_executable(test_dialog_edge_cases test_dialog_edge_cases.cpp)
target_link_libraries(test_dialog_edge_cases PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  sicnu_data
)
target_include_directories(test_dialog_edge_cases PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_dialog_edge_cases)

# Raster processing dialog base — run lifecycle (JobEngine-backed)
sicnu_add_dialog_test(test_raster_processing_dialog_base
  MAIN_SOURCE test_raster_processing_dialog_base_main.cpp
)

# Workflow engine runtime smoke test (Qt-free headers; may link Qt6::Core via sicnu_operators)
if(TARGET sicnu_workflow)
  add_executable(test_workflow_runtime test_workflow_runtime.cpp)
  target_link_libraries(test_workflow_runtime PRIVATE
    Catch2::Catch2WithMain
    sicnu_workflow
    sicnu_workflow_runtime
    sicnu_operators
    sicnu_processing
    sicnu_task_center
    sicnu_jobs
    sicnu_agent
    Sicnu::data
    Qt6::Core
    Qt6::Gui
    qgis_core
    GDAL::GDAL
  )
  if(TARGET jsoncpp)
    target_link_libraries(test_workflow_runtime PRIVATE jsoncpp)
  endif()
  target_include_directories(test_workflow_runtime PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_BINARY_DIR})
  sicnu_discover_tests(test_workflow_runtime)

  add_executable(test_workflow_cancel test_workflow_cancel.cpp)
  target_link_libraries(test_workflow_cancel PRIVATE
    Catch2::Catch2WithMain
    sicnu_workflow
    sicnu_workflow_runtime
    sicnu_operators
    sicnu_processing
    sicnu_task_center
    sicnu_jobs
    sicnu_agent
    Sicnu::data
    Qt6::Core
    Qt6::Gui
    qgis_core
    GDAL::GDAL
  )
  if(TARGET jsoncpp)
    target_link_libraries(test_workflow_cancel PRIVATE jsoncpp)
  endif()
  target_include_directories(test_workflow_cancel PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_BINARY_DIR})
  sicnu_discover_tests(test_workflow_cancel)

  add_executable(test_workflow_execution_plane test_workflow_execution_plane.cpp)
  target_link_libraries(test_workflow_execution_plane PRIVATE
    Catch2::Catch2WithMain
    sicnu_workflow
    sicnu_workflow_runtime
    sicnu_operators
    sicnu_processing
    sicnu_task_center
    sicnu_jobs
    sicnu_agent
    Sicnu::data
    Qt6::Core
    Qt6::Gui
    qgis_core
    GDAL::GDAL
  )
  if(TARGET jsoncpp)
    target_link_libraries(test_workflow_execution_plane PRIVATE jsoncpp)
  elseif(JSONCPP_LIBRARIES)
    target_include_directories(test_workflow_execution_plane PRIVATE ${JSONCPP_INCLUDE_DIRS})
    target_link_libraries(test_workflow_execution_plane PRIVATE ${JSONCPP_LIBRARIES})
  endif()
  target_include_directories(test_workflow_execution_plane PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
  )
  sicnu_discover_tests(test_workflow_execution_plane)

endif()

# JobEngine scheduling tests (submit/cancel/workers + RSOperator execution)
if(TARGET sicnu_jobs)
  add_executable(test_job_engine test_job_engine.cpp)
  target_link_libraries(test_job_engine PRIVATE Catch2::Catch2WithMain sicnu_jobs)
  if(TARGET sicnu_operators)
    target_link_libraries(test_job_engine PRIVATE sicnu_operators)
  endif()
  target_include_directories(test_job_engine PRIVATE ${CMAKE_SOURCE_DIR}/src)
  sicnu_discover_tests(test_job_engine)
endif()

# Data source provider tests — GDAL raster + OGR vector structural resolution
qt_add_executable(test_data_source_providers
  test_data_source_providers.cpp
)
target_link_libraries(test_data_source_providers PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  sicnu_data
)
target_include_directories(test_data_source_providers PRIVATE
  ${CMAKE_SOURCE_DIR}/src
)
sicnu_discover_tests(test_data_source_providers)

# QGIS Display Manager tests — independent QGIS layer instances per Data Asset
qt_add_executable(test_qgis_display_manager
  test_qgis_display_manager.cpp
)
target_link_libraries(test_qgis_display_manager PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
  qgis_core
  qgis_gui
  Sicnu::qgis_display
)
target_include_directories(test_qgis_display_manager PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_qgis_display_manager)

# LayerManager compatibility facade — one legacy load seam into project data/display
qt_add_executable(test_active_view_host_data_context
  test_active_view_host_data_context.cpp
  ${CMAKE_SOURCE_DIR}/src/app/active_view_host.cpp
)
target_link_libraries(test_active_view_host_data_context PRIVATE
  Catch2::Catch2
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  Sicnu::qgis_display
)
target_include_directories(test_active_view_host_data_context PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_active_view_host_data_context)

qt_add_executable(test_active_view_host_viewport
  test_active_view_host_viewport.cpp
  ${CMAKE_SOURCE_DIR}/src/app/active_view_host.cpp
)
target_link_libraries(test_active_view_host_viewport PRIVATE
  Catch2::Catch2
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  Sicnu::qgis_display
)
target_include_directories(test_active_view_host_viewport PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_active_view_host_viewport)

qt_add_executable(test_workflow_session_controller
  test_workflow_session_controller.cpp
  ${CMAKE_SOURCE_DIR}/src/app/shell/workflow_session_controller.cpp
  ${CMAKE_SOURCE_DIR}/src/app/shell/task_panel_host.cpp
  ${CMAKE_SOURCE_DIR}/src/app/shell/schema_form_builder.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/rs_result_summary.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/crs_selector.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workflow/pipeline_port_item.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workflow/pipeline_connection_item.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workflow/pipeline_node_item.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workflow/pipeline_scene.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workflow/pipeline_canvas_widget.cpp
)
target_link_libraries(test_workflow_session_controller PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
  sicnu_operators
  sicnu_workflow
  sicnu_workflow_runtime
  sicnu_task_center
  sicnu_core
  Sicnu::data
  Sicnu::help
)
target_include_directories(test_workflow_session_controller PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_SOURCE_DIR}/src/workflow
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_workflow_session_controller)

qt_add_executable(test_classify_workflow_bridge
  test_classify_workflow_bridge.cpp
  ${CMAKE_SOURCE_DIR}/src/app/classification/rs_classify_workflow_bridge.cpp
  ${CMAKE_SOURCE_DIR}/src/app/classification/rs_classify_workflow_controller.cpp
)
target_link_libraries(test_classify_workflow_bridge PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  sicnu_workflow
  sicnu_workflow_runtime
)
target_include_directories(test_classify_workflow_bridge PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_SOURCE_DIR}/src/workflow
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_classify_workflow_bridge)

if(SICNU_EMBED_PYTHON)
qt_add_executable(test_python_engine
  test_python_engine.cpp
  ${CMAKE_SOURCE_DIR}/src/app/active_view_host.cpp
  ${CMAKE_SOURCE_DIR}/src/python/qgis_python.cpp
  ${CMAKE_SOURCE_DIR}/src/python/sicnu_python_api.cpp
  ${CMAKE_SOURCE_DIR}/src/python/sicnu_python_runner.cpp
)
target_link_libraries(test_python_engine PRIVATE
  Catch2::Catch2
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  Sicnu::qgis_display
  sicnu_processing
  sicnu_task_center
  sicnu_jobs
  sicnu_workflow
  sicnu_workflow_runtime
  Python::Python
  CURL::libcurl
  ${PCRE2_16}
  ${PCRE2_8}
  xml2
)
target_include_directories(test_python_engine PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_SOURCE_DIR}/src/python
  ${CMAKE_BINARY_DIR}
)
target_compile_definitions(test_python_engine PRIVATE
  SICNU_EMBED_PYTHON=1
  TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/data"
)
sicnu_discover_tests(test_python_engine)

qt_add_executable(test_python_plugin_manager
  test_python_plugin_manager.cpp
  ${CMAKE_SOURCE_DIR}/src/app/python/sicnu_app_interface.cpp
  ${CMAKE_SOURCE_DIR}/src/python/qgis_python.cpp
  ${CMAKE_SOURCE_DIR}/src/python/sicnu_python_api.cpp
  ${CMAKE_SOURCE_DIR}/src/python/sicnu_python_runner.cpp
  ${CMAKE_SOURCE_DIR}/src/core/plugin_host.cpp
)
target_link_libraries(test_python_plugin_manager PRIVATE
  Catch2::Catch2
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  sicnu_python_isolated
  sicnu_jobs
  qgis_core
  qgis_gui
  Sicnu::qgis_display
  sicnu_processing
  sicnu_operators
  Python::Python
  CURL::libcurl
  xml2
  jsoncpp
  ${PCRE2_16}
  ${PCRE2_8}
)
target_include_directories(test_python_plugin_manager PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/core
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_SOURCE_DIR}/src/app/python
  ${CMAKE_SOURCE_DIR}/src/python
  ${CMAKE_SOURCE_DIR}/src/python/isolated
  ${CMAKE_BINARY_DIR}
)
target_compile_definitions(test_python_plugin_manager PRIVATE
  SICNU_EMBED_PYTHON=1
  TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/data"
)
# RUN_SERIAL: each binary spawns its own embedded Python + worker_daemon.py
# subprocesses; running several Python-initializing binaries concurrently
# under `ctest -j4` races Python runtime startup and causes flaky IPC
# timeouts. Serialize them so no two Python worker pools run at once.
sicnu_discover_tests(test_python_plugin_manager RUN_SERIAL TRUE)

qt_add_executable(test_plugin_host
  test_plugin_host.cpp
  ${CMAKE_SOURCE_DIR}/src/app/python/sicnu_app_interface.cpp
  ${CMAKE_SOURCE_DIR}/src/python/qgis_python.cpp
  ${CMAKE_SOURCE_DIR}/src/python/sicnu_python_api.cpp
  ${CMAKE_SOURCE_DIR}/src/python/sicnu_python_runner.cpp
  ${CMAKE_SOURCE_DIR}/src/core/plugin_host.cpp
)
target_link_libraries(test_plugin_host PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  sicnu_python_isolated
  sicnu_jobs
  qgis_core
  qgis_gui
  Sicnu::qgis_display
  sicnu_processing
  sicnu_operators
  Python::Python
  CURL::libcurl
  xml2
  jsoncpp
  ${PCRE2_16}
  ${PCRE2_8}
)
target_include_directories(test_plugin_host PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/core
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_SOURCE_DIR}/src/app/python
  ${CMAKE_SOURCE_DIR}/src/python
  ${CMAKE_SOURCE_DIR}/src/python/isolated
  ${CMAKE_BINARY_DIR}
)
target_compile_definitions(test_plugin_host PRIVATE
  SICNU_EMBED_PYTHON=1
  TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/data"
)
sicnu_discover_tests(test_plugin_host)

qt_add_executable(test_python_plugin_host
  test_python_plugin_host.cpp
  ${CMAKE_SOURCE_DIR}/src/cli/rs_pipeline_runner.cpp
  ${CMAKE_SOURCE_DIR}/src/core/plugin_host.cpp
  ${CMAKE_SOURCE_DIR}/src/app/python/sicnu_app_interface.cpp
)
target_link_libraries(test_python_plugin_host PRIVATE
  Catch2::Catch2
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  sicnu_python_isolated
  Sicnu::qgis_display
  sicnu_processing
  sicnu_operators
  sicnu_task_center
  sicnu_jobs
  sicnu_workflow
  sicnu_workflow_runtime
  Sicnu::data
  qgis_core
  GDAL::GDAL
)
target_include_directories(test_python_plugin_host PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/core
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_SOURCE_DIR}/src/python/isolated
  ${CMAKE_BINARY_DIR}
)
target_compile_definitions(test_python_plugin_host PRIVATE
  SICNU_EMBED_PYTHON=1
  TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/data"
)
sicnu_discover_tests(test_python_plugin_host RUN_SERIAL TRUE)

qt_add_executable(test_python_processing_provider
  test_python_processing_provider.cpp
  ${CMAKE_SOURCE_DIR}/src/app/active_view_host.cpp
  ${CMAKE_SOURCE_DIR}/src/python/qgis_python.cpp
  ${CMAKE_SOURCE_DIR}/src/python/sicnu_python_api.cpp
  ${CMAKE_SOURCE_DIR}/src/python/sicnu_python_runner.cpp
)
target_link_libraries(test_python_processing_provider PRIVATE
  Catch2::Catch2
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
  jsoncpp
  Sicnu::qgis_display
  Python::Python
  CURL::libcurl
  ${PCRE2_16}
  ${PCRE2_8}
)
target_include_directories(test_python_processing_provider PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_SOURCE_DIR}/src/python
  ${CMAKE_BINARY_DIR}
)
target_compile_definitions(test_python_processing_provider PRIVATE
  SICNU_EMBED_PYTHON=1
  TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/data"
)
sicnu_discover_tests(test_python_processing_provider)

qt_add_executable(test_python_script_editor
  test_python_script_editor.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/python_script_editor.cpp
  ${CMAKE_SOURCE_DIR}/src/python/sicnu_python_runner.cpp
  ${CMAKE_SOURCE_DIR}/src/python/qgis_python.cpp
  ${CMAKE_SOURCE_DIR}/src/python/sicnu_python_api.cpp
  ${CMAKE_SOURCE_DIR}/src/app/active_view_host.cpp
)
target_link_libraries(test_python_script_editor PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
  qgis_core
  qgis_gui
  Sicnu::qgis_display
  Python::Python
  CURL::libcurl
  ${PCRE2_16}
  ${PCRE2_8}
  xml2
)
target_include_directories(test_python_script_editor PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_SOURCE_DIR}/src/python
  ${CMAKE_BINARY_DIR}
)
target_compile_definitions(test_python_script_editor PRIVATE
  SICNU_EMBED_PYTHON=1
  TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/data"
)
sicnu_discover_tests(test_python_script_editor RUN_SERIAL TRUE)
endif()

sicnu_add_test(test_llm_streaming_client)
target_link_libraries(test_llm_streaming_client PRIVATE
  sicnu_agent
  sicnu_processing
)

sicnu_add_test(test_agent_copilot_ui)
target_link_libraries(test_agent_copilot_ui PRIVATE
  sicnu_agent
  sicnu_data
  sicnu_processing
  GDAL::GDAL
)

qt_add_executable(test_agent_canvas_sync
  test_agent_canvas_sync.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workflow/pipeline_canvas_widget.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workflow/pipeline_scene.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workflow/pipeline_node_item.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workflow/pipeline_port_item.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workflow/pipeline_connection_item.cpp
)
target_link_libraries(test_agent_canvas_sync PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_agent
  sicnu_workflow
  sicnu_workflow_runtime
  sicnu_processing
)
target_include_directories(test_agent_canvas_sync PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_SOURCE_DIR}/src/app/workflow
)
sicnu_discover_tests(test_agent_canvas_sync)

# Project persistence — SICNU Data identity plus standard QGIS presentation
qt_add_executable(test_data_project_roundtrip
  test_data_project_roundtrip.cpp
)
target_link_libraries(test_data_project_roundtrip PRIVATE
  Catch2::Catch2
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Xml
  qgis_core
  qgis_gui
  Sicnu::qgis_display
)
target_include_directories(test_data_project_roundtrip PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_data_project_roundtrip)

# Workspace Governance 3.0 — project format v3 (roundtrip, migration,
# forward tolerance, atomic-save hygiene).
qt_add_executable(test_workspace_project_v3
  test_workspace_project_v3.cpp
)
target_link_libraries(test_workspace_project_v3 PRIVATE
  Catch2::Catch2
  Qt6::Core  Qt6::Gui  Qt6::Widgets  Qt6::Xml
  qgis_core  qgis_gui
  Sicnu::qgis_display
)
target_include_directories(test_workspace_project_v3 PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_workspace_project_v3)

# Data Manager panel tests — asset catalog projection, separate from layer tree
add_executable(test_data_manager_panel
  test_data_manager_panel.cpp
  ${CMAKE_SOURCE_DIR}/src/app/panels/data_manager_panel.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/rs_empty_state_widget.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/dialog_help_catalog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/dialog_utils.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/temporal_analysis_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/raster_processing_dialog_base.cpp
  ${CMAKE_SOURCE_DIR}/src/app/shell/gui_job_adapter.cpp
  ${CMAKE_SOURCE_DIR}/src/app/shell/processing_job_adapter.cpp
)
target_link_libraries(test_data_manager_panel PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
  qgis_core
  qgis_gui
  sicnu_processing
  sicnu_task_center
  sicnu_data
  sicnu_jobs
  sicnu_operators
  Sicnu::qgis_display
)
target_include_directories(test_data_manager_panel PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_data_manager_panel PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_data_manager_panel)

# Desktop Workbench UX 4.0 — 工作区治理 panel wiring contract (Milestone A)
add_executable(test_workspace_browser_wiring
  test_workspace_browser_wiring.cpp
  ${CMAKE_SOURCE_DIR}/src/app/panels/workspace_browser_panel.cpp
)
target_link_libraries(test_workspace_browser_wiring PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
  qgis_core
  qgis_gui
  sicnu_processing
  sicnu_task_center
  sicnu_data
  sicnu_jobs
  sicnu_operators
)
target_include_directories(test_workspace_browser_wiring PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
target_compile_definitions(test_workspace_browser_wiring PRIVATE
  CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
set_target_properties(test_workspace_browser_wiring PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_workspace_browser_wiring)

# Adoption safety net — legacy QGIS layers adopted into the Data Manager
add_executable(test_layer_adoption_safety_net
  test_layer_adoption_safety_net.cpp
)
target_link_libraries(test_layer_adoption_safety_net PRIVATE
  Catch2::Catch2
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  Sicnu::qgis_display
)
target_include_directories(test_layer_adoption_safety_net PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_layer_adoption_safety_net)

# Phase 1 first-deliverable acceptance — end-to-end data/display separation
add_executable(test_data_phase1_acceptance
  test_data_phase1_acceptance.cpp
)
target_link_libraries(test_data_phase1_acceptance PRIVATE
  Catch2::Catch2
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Xml
  qgis_core
  qgis_gui
  Sicnu::qgis_display
)
target_include_directories(test_data_phase1_acceptance PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_data_phase1_acceptance)

# Processing Asset Resolver tests — AssetRef to immutable snapshot with Task lease
qt_add_executable(test_processing_asset_resolver
  test_processing_asset_resolver.cpp
)
target_link_libraries(test_processing_asset_resolver PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  sicnu_data
)
target_include_directories(test_processing_asset_resolver PRIVATE
  ${CMAKE_SOURCE_DIR}/src
)
sicnu_discover_tests(test_processing_asset_resolver)

# Derivation Record tests — serializable provenance value, credential exclusion
qt_add_executable(test_derivation_record
  test_derivation_record.cpp
)
target_link_libraries(test_derivation_record PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  sicnu_data
)
target_include_directories(test_derivation_record PRIVATE
  ${CMAKE_SOURCE_DIR}/src
)
sicnu_discover_tests(test_derivation_record)

# Output Committer tests — transactional algorithm output commit via Task Center seam
qt_add_executable(test_output_committer
  test_output_committer.cpp
)
target_link_libraries(test_output_committer PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Test
  qgis_core
  qgis_gui
  sicnu_processing
  sicnu_task_center
  sicnu_data
)
target_include_directories(test_output_committer PRIVATE
  ${CMAKE_SOURCE_DIR}/src
)
sicnu_discover_tests(test_output_committer)

# Spectral-index asset pipeline tests — first real caller of resolver + committer
qt_add_executable(test_spectral_index_asset_pipeline
  test_spectral_index_asset_pipeline.cpp
)
target_link_libraries(test_spectral_index_asset_pipeline PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Test
  qgis_core
  qgis_gui
  sicnu_operators
  sicnu_processing
  sicnu_data
)
target_include_directories(test_spectral_index_asset_pipeline PRIVATE
  ${CMAKE_SOURCE_DIR}/src
)
sicnu_discover_tests(test_spectral_index_asset_pipeline)

# Data Manager reap tests - temporary-asset reaping with DeletableSource
qt_add_executable(test_data_manager_reap
  test_data_manager_reap.cpp
)
target_link_libraries(test_data_manager_reap PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Test
  qgis_core
  sicnu_processing
  sicnu_data
)
target_include_directories(test_data_manager_reap PRIVATE
  ${CMAKE_SOURCE_DIR}/src
)
sicnu_discover_tests(test_data_manager_reap)

# ProjectContext session-close reap tests
add_executable(test_project_context_reap
  test_project_context_reap.cpp
)
target_link_libraries(test_project_context_reap PRIVATE
  Catch2::Catch2
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  Sicnu::qgis_display
  Sicnu::session_map
)
target_include_directories(test_project_context_reap PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_project_context_reap)

# Data Manager promote tests - temporary -> ProjectPersistent
qt_add_executable(test_data_manager_promote
  test_data_manager_promote.cpp
)
target_link_libraries(test_data_manager_promote PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Test
  qgis_core
  sicnu_data
)
target_include_directories(test_data_manager_promote PRIVATE
  ${CMAKE_SOURCE_DIR}/src
)
sicnu_discover_tests(test_data_manager_promote)

# Data Manager collection tests - Data Collection catalog node
qt_add_executable(test_data_manager_collection
  test_data_manager_collection.cpp
)
target_link_libraries(test_data_manager_collection PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Test
  qgis_core
  sicnu_data
)
target_include_directories(test_data_manager_collection PRIVATE
  ${CMAKE_SOURCE_DIR}/src
)
sicnu_discover_tests(test_data_manager_collection)

# Collection import service tests - read-only probe with stub + real discoverer
qt_add_executable(test_collection_import_service
  test_collection_import_service.cpp
)
target_link_libraries(test_collection_import_service PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Test
  qgis_core
  sicnu_processing
  sicnu_data
)
target_include_directories(test_collection_import_service PRIVATE
  ${CMAKE_SOURCE_DIR}/src
)
sicnu_discover_tests(test_collection_import_service)

# Satellite product discovery/import tests (Landsat / Sentinel-2 / MODIS) —
# discovery, stacking, semantic band-role metadata, and the import operators.
# This file was previously an orphan (registered in no target); registered here
# so the discovery + stacking + role-metadata coverage actually runs.
add_executable(test_satellite_products test_satellite_products.cpp)
target_link_libraries(test_satellite_products PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Test
  qgis_core
  sicnu_processing
  sicnu_operators
  GDAL::GDAL
)
target_include_directories(test_satellite_products PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_link_jsoncpp(test_satellite_products)
sicnu_discover_tests(test_satellite_products)

# Product import dialog tests - probe-preview-commit transaction (headless;
# Landsat + Sentinel-2 fixtures, sensor-agnostic dialog).
add_executable(test_product_import_dialog
  test_product_import_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/product_import_dialog.cpp
  # product_import_dialog.cpp calls SicnuDialogHelp::htmlForTool/shortForTool/tip,
  # whose impls live in dialog_help_catalog.cpp. The other dialog tests
  # (lines ~1739/2844/3267) already compile it; this target was missing it,
  # which left the dialog import test unlinked (_NOT_BUILT in ctest).
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/dialog_help_catalog.cpp
)
target_link_libraries(test_product_import_dialog PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
  qgis_core
  qgis_gui
  sicnu_processing
  sicnu_data
)
target_include_directories(test_product_import_dialog PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_product_import_dialog PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_product_import_dialog)

# Orthorectification dialog tests - headless param building over the
# gdal:orthorectification operator contract (mirrors the base-class test's
# JobEngine-backed dependency chain).
sicnu_add_dialog_test(test_orthorectification_dialog
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/orthorectification_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/crs_selector.cpp
)

# Batch processing dialog — RS-operator listing and per-file batch execution
add_executable(test_batch_processing_dialog
  test_batch_processing_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/batch_processing_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/dialog_help_catalog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/dialog_utils.cpp
)
target_link_libraries(test_batch_processing_dialog PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
  Qt6::Concurrent
  sicnu_jobs
  sicnu_operators
  sicnu_task_center
  qgis_core
  qgis_gui
  sicnu_processing
  sicnu_data
)
target_include_directories(test_batch_processing_dialog PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
sicnu_link_jsoncpp(test_batch_processing_dialog)
set_target_properties(test_batch_processing_dialog PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_batch_processing_dialog)

# Dialog grid-preflight helper — shared pre-run compatibility check
add_executable(test_grid_compat_dialog_utils
  test_grid_compat_dialog_utils.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/dialog_utils.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/dialog_help_catalog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/resolution_widget.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/raster_layer_combo.cpp
)
target_link_libraries(test_grid_compat_dialog_utils PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
  Qt6::Concurrent
  sicnu_jobs
  sicnu_operators
  sicnu_task_center
  qgis_core
  qgis_gui
  sicnu_processing
  sicnu_data
)
target_include_directories(test_grid_compat_dialog_utils PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
sicnu_link_jsoncpp(test_grid_compat_dialog_utils)
set_target_properties(test_grid_compat_dialog_utils PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_grid_compat_dialog_utils)

# Change-detection dialog — method/mask parameter alignment with the backend
sicnu_add_dialog_test(test_change_detection_dialog
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/change_detection_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/comparison_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/comparison_widget.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/raster_layer_combo.cpp
)

# Apply-mask dialog — operator JSON assembly from widget state
sicnu_add_dialog_test(test_apply_mask_dialog
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/apply_mask_dialog.cpp
)

# Spectral library matching dialog — SAM-ranked match table + save round trip
add_executable(test_spectral_library_dialog
  test_spectral_library_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/spectral_library_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/dialog_help_catalog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/dialog_utils.cpp
)
target_link_libraries(test_spectral_library_dialog PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
  Qt6::Concurrent
  sicnu_jobs
  sicnu_operators
  sicnu_task_center
  qgis_core
  qgis_gui
  sicnu_processing
  sicnu_data
)
target_include_directories(test_spectral_library_dialog PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
sicnu_link_jsoncpp(test_spectral_library_dialog)
set_target_properties(test_spectral_library_dialog PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_spectral_library_dialog)

# Post-classification comparison dialog — operator JSON assembly
sicnu_add_dialog_test(test_post_classification_dialog
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/post_classification_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/raster_layer_combo.cpp
)

# Shared band-role selector widget (C5)
add_executable(test_band_role_combo
  test_band_role_combo.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/band_role_combo.cpp
  ${CMAKE_SOURCE_DIR}/src/processing/gdal/gdal_dataset_wrapper.cpp
)
target_link_libraries(test_band_role_combo PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
  GDAL::GDAL
)
target_include_directories(test_band_role_combo PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_band_role_combo)

# Shared raster layer picker widget (C5)
add_executable(test_raster_layer_combo
  test_raster_layer_combo.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/raster_layer_combo.cpp
  ${CMAKE_SOURCE_DIR}/src/processing/gdal/gdal_dataset_wrapper.cpp
)
target_link_libraries(test_raster_layer_combo PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
  GDAL::GDAL
)
target_include_directories(test_raster_layer_combo PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_raster_layer_combo)

# Shared CRS input widget (C5)
add_executable(test_crs_selector
  test_crs_selector.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/crs_selector.cpp
)
target_link_libraries(test_crs_selector PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_data
)
target_include_directories(test_crs_selector PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_crs_selector)

# Schema-driven Operator UI (UX 4.0 Milestone B)
add_executable(test_schema_form_builder_v2
  test_schema_form_builder_v2.cpp
  ${CMAKE_SOURCE_DIR}/src/app/shell/schema_form_builder.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/crs_selector.cpp
)
target_link_libraries(test_schema_form_builder_v2 PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
  sicnu_operators
  sicnu_data
  GDAL::GDAL
  Sicnu::help
)
target_include_directories(test_schema_form_builder_v2 PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
sicnu_link_jsoncpp(test_schema_form_builder_v2)
set_target_properties(test_schema_form_builder_v2 PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_schema_form_builder_v2)

# Shared RsResultSummary widget (UX 4.0 Milestone E)
add_executable(test_rs_result_summary
  test_rs_result_summary.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/rs_result_summary.cpp
)
target_link_libraries(test_rs_result_summary PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
)
target_include_directories(test_rs_result_summary PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
sicnu_link_jsoncpp(test_rs_result_summary)
set_target_properties(test_rs_result_summary PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_rs_result_summary)

# Keyboard conflict guardrail (UX 4.0 Milestone G)
add_executable(test_shortcut_conflicts
  test_shortcut_conflicts.cpp
)
target_link_libraries(test_shortcut_conflicts PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
)
target_include_directories(test_shortcut_conflicts PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
target_compile_definitions(test_shortcut_conflicts PRIVATE
  CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
sicnu_discover_tests(test_shortcut_conflicts)

# ── Workbench 6.0 Milestone C: bounded UI scan pool (#797) ──────────────────
add_executable(test_scan_pool
  test_scan_pool.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/rs_scan_pool.cpp
)
target_link_libraries(test_scan_pool PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
)
target_include_directories(test_scan_pool PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
target_compile_definitions(test_scan_pool PRIVATE
  CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
sicnu_discover_tests(test_scan_pool)

# Spectral index dialog — role-based band preselection via BandRoleCombo (C5)
sicnu_add_dialog_test(test_spectral_index_dialog
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/spectral_index_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/band_role_combo.cpp
)

# M2 processing dialogs empirical stress and adversarial testing suite
sicnu_add_dialog_test(test_m2_dialog_stress
  MAIN_SOURCE test_raster_processing_dialog_base_main.cpp
  SOURCES
    ${CMAKE_SOURCE_DIR}/src/app/dialogs/atmospheric_dialog.cpp
    ${CMAKE_SOURCE_DIR}/src/app/dialogs/radiometric_calibration_dialog.cpp
    ${CMAKE_SOURCE_DIR}/src/app/dialogs/contrast_stretch_dialog.cpp
    ${CMAKE_SOURCE_DIR}/src/app/widgets/histogram_stretch_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/app/widgets/histogram_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/app/dialogs/spatial_filter_dialog.cpp
    ${CMAKE_SOURCE_DIR}/src/app/dialogs/speckle_filter_dialog.cpp
    ${CMAKE_SOURCE_DIR}/src/app/dialogs/spectral_index_dialog.cpp
    ${CMAKE_SOURCE_DIR}/src/app/dialogs/spectral_library_dialog.cpp
    ${CMAKE_SOURCE_DIR}/src/app/dialogs/band_ratio_dialog.cpp
    ${CMAKE_SOURCE_DIR}/src/app/dialogs/band_math_dialog.cpp
    ${CMAKE_SOURCE_DIR}/src/app/dialogs/extract_band_dialog.cpp
    ${CMAKE_SOURCE_DIR}/src/app/dialogs/qa_mask_dialog.cpp
    ${CMAKE_SOURCE_DIR}/src/app/dialogs/orthorectification_dialog.cpp
    ${CMAKE_SOURCE_DIR}/src/app/widgets/crs_selector.cpp
    ${CMAKE_SOURCE_DIR}/src/app/dialogs/mosaic_dialog.cpp
    ${CMAKE_SOURCE_DIR}/src/app/dialogs/fusion_dialog.cpp
    ${CMAKE_SOURCE_DIR}/src/app/dialogs/change_detection_dialog.cpp
    ${CMAKE_SOURCE_DIR}/src/app/dialogs/comparison_dialog.cpp
    ${CMAKE_SOURCE_DIR}/src/app/widgets/comparison_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/app/dialogs/pca_dialog.cpp
    ${CMAKE_SOURCE_DIR}/src/app/dialogs/post_classification_dialog.cpp
    ${CMAKE_SOURCE_DIR}/src/app/dialogs/terrain_dialog.cpp
    ${CMAKE_SOURCE_DIR}/src/app/dialogs/apply_mask_dialog.cpp
    ${CMAKE_SOURCE_DIR}/src/app/dialogs/batch_processing_dialog.cpp
    ${CMAKE_SOURCE_DIR}/src/app/dialogs/product_import_dialog.cpp
    ${CMAKE_SOURCE_DIR}/src/app/dialogs/crs_preset_dialog.cpp
    ${CMAKE_SOURCE_DIR}/src/app/crs_presets.cpp
    ${CMAKE_SOURCE_DIR}/src/app/dialogs/help_viewer_dialog.cpp
    ${CMAKE_SOURCE_DIR}/src/app/dialogs/preferences_dialog.cpp
    ${CMAKE_SOURCE_DIR}/src/app/classification/rs_merge_classes_dialog.cpp
    ${CMAKE_SOURCE_DIR}/src/app/classification/rs_post_process_dialog.cpp
    ${CMAKE_SOURCE_DIR}/src/app/classification/rs_post_process_task.cpp
    ${CMAKE_SOURCE_DIR}/src/app/georeferencer/rs_sift_dialog.cpp
    ${CMAKE_SOURCE_DIR}/src/app/georeferencer/rs_template_match_dialog.cpp
    ${CMAKE_SOURCE_DIR}/src/app/widgets/band_role_combo.cpp
    ${CMAKE_SOURCE_DIR}/src/processing/gdal/gdal_dataset_wrapper.cpp
    ${CMAKE_SOURCE_DIR}/src/app/dialogs/async_gdal_runner.cpp
    ${CMAKE_SOURCE_DIR}/src/app/dialogs/async_runner_base.cpp
    ${CMAKE_SOURCE_DIR}/src/app/dialogs/async_algorithm_runner.cpp
  EXTRA_LIBS
    GDAL::GDAL
    Sicnu::qgis_display
  EXTRA_INCLUDES
    ${CMAKE_SOURCE_DIR}/src/app/classification
    ${CMAKE_SOURCE_DIR}/src/app/georeferencer
)

# Virtual raster recipe tests - recipe value type + JSON round-trip (#55)
qt_add_executable(test_virtual_raster_recipe
  test_virtual_raster_recipe.cpp
)
target_link_libraries(test_virtual_raster_recipe PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  sicnu_data
)
target_include_directories(test_virtual_raster_recipe PRIVATE
  ${CMAKE_SOURCE_DIR}/src
)
sicnu_discover_tests(test_virtual_raster_recipe)

# Remote map structure model tests - RemoteMapStructure + AssetStructure arm (#61)
qt_add_executable(test_remote_map_structure
  test_remote_map_structure.cpp
)
target_link_libraries(test_remote_map_structure PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  sicnu_data
)
target_include_directories(test_remote_map_structure PRIVATE
  ${CMAKE_SOURCE_DIR}/src
)
sicnu_discover_tests(test_remote_map_structure)

# XYZ source provider + NetworkProbe + registration tests (#62)
qt_add_executable(test_xyz_source_provider
  test_xyz_source_provider.cpp
)
target_link_libraries(test_xyz_source_provider PRIVATE
  Catch2::Catch2WithMain
  Catch2::Catch2
  Qt6::Core
  sicnu_data
)
target_include_directories(test_xyz_source_provider PRIVATE
  ${CMAKE_SOURCE_DIR}/src
)
sicnu_discover_tests(test_xyz_source_provider)

# WMS/WMTS/TMS source providers - shared XYZ shape (#63)
qt_add_executable(test_remote_map_services
  test_remote_map_services.cpp
)
target_link_libraries(test_remote_map_services PRIVATE
  Catch2::Catch2WithMain
  Catch2::Catch2
  Qt6::Core
  sicnu_data
)
target_include_directories(test_remote_map_services PRIVATE
  ${CMAKE_SOURCE_DIR}/src
)
sicnu_discover_tests(test_remote_map_services)

# Remote-map display materialization + AuthResolver seam (#64)
qt_add_executable(test_remote_map_display
  test_remote_map_display.cpp
)
target_link_libraries(test_remote_map_display PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  Sicnu::qgis_display
  sicnu_data
)
target_include_directories(test_remote_map_display PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_remote_map_display)

# Per-service remote-map URI builder (pure function) (#64)
qt_add_executable(test_remote_map_uri
  test_remote_map_uri.cpp
)
target_link_libraries(test_remote_map_uri PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Sicnu::qgis_display
)
target_include_directories(test_remote_map_uri PRIVATE
  ${CMAKE_SOURCE_DIR}/src
)
sicnu_discover_tests(test_remote_map_uri)

# Data Manager dependency tests - strong-dependency DAG (#56)
qt_add_executable(test_data_manager_dependency
  test_data_manager_dependency.cpp
)
target_link_libraries(test_data_manager_dependency PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Test
  qgis_core
  sicnu_data
)
target_include_directories(test_data_manager_dependency PRIVATE
  ${CMAKE_SOURCE_DIR}/src
)
sicnu_discover_tests(test_data_manager_dependency)

# Virtual raster preflight tests - pure classification over snapshots (#57)
qt_add_executable(test_virtual_raster_preflight
  test_virtual_raster_preflight.cpp
)
target_link_libraries(test_virtual_raster_preflight PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Test
  qgis_core
  sicnu_processing
  sicnu_data
)
target_include_directories(test_virtual_raster_preflight PRIVATE
  ${CMAKE_SOURCE_DIR}/src
)
sicnu_discover_tests(test_virtual_raster_preflight)

# Shared raster-grid compatibility service tests (pure structure comparisons).
qt_add_executable(test_raster_grid_compat
  test_raster_grid_compat.cpp
)
target_link_libraries(test_raster_grid_compat PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  sicnu_data
)
target_include_directories(test_raster_grid_compat PRIVATE
  ${CMAKE_SOURCE_DIR}/src
)
sicnu_discover_tests(test_raster_grid_compat)

# Virtual raster creation tests - provider + createVirtualRaster (#58)
add_executable(test_virtual_raster_create
  test_virtual_raster_create.cpp
)
target_link_libraries(test_virtual_raster_create PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
  qgis_core
  qgis_gui
  Sicnu::qgis_display
  sicnu_data
)
target_include_directories(test_virtual_raster_create PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_virtual_raster_create PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_virtual_raster_create)

# #66 real NetworkProbe: pure GetCapabilities XML parser (TDD slice 1). The
# parser lives in sicnu_qgis_display; the test links it (transitively pulls
# Qt6::Xml + Sicnu::data).
qt_add_executable(test_remote_map_capabilities_parser
  test_remote_map_capabilities_parser.cpp
)
target_link_libraries(test_remote_map_capabilities_parser PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Sicnu::qgis_display
)
target_include_directories(test_remote_map_capabilities_parser PRIVATE
  ${CMAKE_SOURCE_DIR}/src
)
sicnu_discover_tests(test_remote_map_capabilities_parser)

# #66 real NetworkProbe: the concrete QgisNetworkProbe with an injected stub
# fetcher (TDD slice 2). The probe + parser live in sicnu_qgis_display.
qt_add_executable(test_network_probe
  test_network_probe.cpp
)
target_link_libraries(test_network_probe PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Sicnu::qgis_display
)
target_include_directories(test_network_probe PRIVATE
  ${CMAKE_SOURCE_DIR}/src
)
sicnu_discover_tests(test_network_probe)

# #66 real NetworkProbe: end-to-end host wiring (TDD slice 3). The probe +
# parser live in sicnu_qgis_display, which also owns project_context.cpp.
qt_add_executable(test_project_context_remote_probe
  test_project_context_remote_probe.cpp
)
target_link_libraries(test_project_context_remote_probe PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Widgets
  qgis_core
  qgis_gui
  Sicnu::qgis_display
)
target_include_directories(test_project_context_remote_probe PRIVATE
  ${CMAKE_SOURCE_DIR}/src
)
sicnu_discover_tests(test_project_context_remote_probe)

qt_add_executable(test_workflow_pipeline
  test_workflow_pipeline.cpp
)
target_link_libraries(test_workflow_pipeline PRIVATE
  Catch2::Catch2
  Qt6::Core
  sicnu_workflow
  sicnu_workflow_runtime
)
target_include_directories(test_workflow_pipeline PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/workflow
)
sicnu_discover_tests(test_workflow_pipeline)

qt_add_executable(test_workflow_pipeline_ui
  test_workflow_pipeline_ui.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workflow/pipeline_port_item.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workflow/pipeline_connection_item.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workflow/pipeline_node_item.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workflow/pipeline_scene.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workflow/pipeline_canvas_widget.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workflow/preset_catalog_widget.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workflow/pipeline_editor_dock.cpp
  ${CMAKE_SOURCE_DIR}/src/app/shell/workflow_session_controller.cpp
  ${CMAKE_SOURCE_DIR}/src/app/shell/task_panel_host.cpp
  ${CMAKE_SOURCE_DIR}/src/app/shell/schema_form_builder.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/rs_result_summary.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/crs_selector.cpp
)
target_link_libraries(test_workflow_pipeline_ui PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
  sicnu_operators
  sicnu_workflow
  sicnu_workflow_runtime
  sicnu_task_center
  sicnu_data
  Sicnu::help
)
target_include_directories(test_workflow_pipeline_ui PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_SOURCE_DIR}/src/workflow
)
sicnu_discover_tests(test_workflow_pipeline_ui)

# Production Golden E2E Optical Remote Sensing Test
add_executable(test_rs_golden_e2e test_rs_golden_e2e.cpp)
target_link_libraries(test_rs_golden_e2e PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  qgis_analysis
  sicnu_processing
  sicnu_operators
  sicnu_data
  GDAL::GDAL
)
target_include_directories(test_rs_golden_e2e PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
if(OpenCV_FOUND)
  target_include_directories(test_rs_golden_e2e PRIVATE ${OpenCV_INCLUDE_DIRS})
  target_link_libraries(test_rs_golden_e2e PRIVATE ${OpenCV_LIBRARIES})
endif()
sicnu_link_jsoncpp(test_rs_golden_e2e)
set_target_properties(test_rs_golden_e2e PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_rs_golden_e2e)

add_executable(test_image_enhancement_panel_band_ratio test_image_enhancement_panel_band_ratio.cpp)
target_link_libraries(test_image_enhancement_panel_band_ratio PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
  GDAL::GDAL
)
target_include_directories(test_image_enhancement_panel_band_ratio PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_image_enhancement_panel_band_ratio)

add_executable(test_image_enhancement_panel_ihs test_image_enhancement_panel_ihs.cpp)
target_link_libraries(test_image_enhancement_panel_ihs PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
  GDAL::GDAL
)
target_include_directories(test_image_enhancement_panel_ihs PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_image_enhancement_panel_ihs)

add_executable(test_subsystem_stability test_subsystem_stability.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/histogram_widget.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/rs_scan_pool.cpp
)
target_link_libraries(test_subsystem_stability PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  qgis_analysis
  sicnu_data
  sicnu_processing
  sicnu_jobs
  sicnu_task_center
  sicnu_agent
  sicnu_operators
  sicnu_workflow
  sicnu_workflow_runtime
  GDAL::GDAL
)
sicnu_link_jsoncpp(test_subsystem_stability)
target_include_directories(test_subsystem_stability PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_subsystem_stability PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_subsystem_stability)

add_executable(test_e2e_phase2 test_e2e_phase2.cpp)
target_link_libraries(test_e2e_phase2 PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  qgis_analysis
  sicnu_data
  sicnu_processing
  sicnu_jobs
  sicnu_task_center
  sicnu_agent
  sicnu_operators
  sicnu_workflow
  sicnu_workflow_runtime
  GDAL::GDAL
)
sicnu_link_jsoncpp(test_e2e_phase2)
target_include_directories(test_e2e_phase2 PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_e2e_phase2 PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_e2e_phase2)

# ---------------------------------------------------------------------------
# E2E Test Suite for Open Issues #773 - #817
# ---------------------------------------------------------------------------
add_executable(test_e2e_open_issues
  test_e2e_open_issues.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/command_registry.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/selection_context.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/inspector_host.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/workbench_host.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/adapters.cpp
)
target_link_libraries(test_e2e_open_issues PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
  qgis_core
  qgis_gui
  qgis_analysis
  Sicnu::qgis_display
  Sicnu::dataset
  Sicnu::experiment
  Sicnu::Geospatial
  sicnu_data
  sicnu_processing
  sicnu_jobs
  sicnu_task_center
  sicnu_agent
  sicnu_operators
  sicnu_workflow
  sicnu_workflow_runtime
  GDAL::GDAL
)
sicnu_link_jsoncpp(test_e2e_open_issues)
target_include_directories(test_e2e_open_issues PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
target_compile_definitions(test_e2e_open_issues PRIVATE
  CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
set_target_properties(test_e2e_open_issues PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_e2e_open_issues)

# ---------------------------------------------------------------------------
# Workflow Engine 2.0 test suites
# ---------------------------------------------------------------------------
add_executable(test_workflow_engine_v2 test_workflow_engine_v2.cpp)
target_link_libraries(test_workflow_engine_v2 PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  sicnu_workflow
  sicnu_workflow_runtime
  sicnu_data
  sicnu_processing
  sicnu_task_center
  sicnu_jobs
)
if(TARGET jsoncpp)
  target_link_libraries(test_workflow_engine_v2 PRIVATE jsoncpp)
endif()
target_include_directories(test_workflow_engine_v2 PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_workflow_engine_v2)

add_executable(test_workflow_incremental_cache test_workflow_incremental_cache.cpp)
target_link_libraries(test_workflow_incremental_cache PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  sicnu_data
)
target_include_directories(test_workflow_incremental_cache PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_workflow_incremental_cache)

add_executable(test_workflow_recovery test_workflow_recovery.cpp)
target_link_libraries(test_workflow_recovery PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  sicnu_workflow
  sicnu_workflow_runtime
  sicnu_data
  sicnu_processing
  sicnu_task_center
  sicnu_jobs
)
if(TARGET jsoncpp)
  target_link_libraries(test_workflow_recovery PRIVATE jsoncpp)
endif()
target_include_directories(test_workflow_recovery PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_workflow_recovery)

add_executable(test_workflow_resume_provenance test_workflow_resume_provenance.cpp
  ${CMAKE_SOURCE_DIR}/src/cli/rs_pipeline_runner.cpp
)
target_link_libraries(test_workflow_resume_provenance PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  sicnu_operators
  sicnu_processing
  sicnu_task_center
  sicnu_jobs
  sicnu_workflow
  sicnu_workflow_runtime
  Sicnu::qgis_display
  Sicnu::data
  qgis_core
  GDAL::GDAL
)
target_include_directories(test_workflow_resume_provenance PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/cli
  ${CMAKE_BINARY_DIR}
)
if(TARGET jsoncpp)
  target_link_libraries(test_workflow_resume_provenance PRIVATE jsoncpp)
endif()
if(SICNU_EMBED_PYTHON)
  target_sources(test_workflow_resume_provenance PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core/plugin_host.cpp
    ${CMAKE_SOURCE_DIR}/src/app/python/sicnu_app_interface.cpp
  )
  target_link_libraries(test_workflow_resume_provenance PRIVATE sicnu_python_isolated)
  target_include_directories(test_workflow_resume_provenance PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/app
  )
  target_compile_definitions(test_workflow_resume_provenance PRIVATE SICNU_EMBED_PYTHON=1)
endif()
sicnu_link_jsoncpp(test_workflow_resume_provenance)
set_target_properties(test_workflow_resume_provenance PROPERTIES RUN_SERIAL TRUE)
sicnu_discover_tests(test_workflow_resume_provenance)

add_executable(test_workflow_run_coordinator test_workflow_run_coordinator.cpp)
target_link_libraries(test_workflow_run_coordinator PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  sicnu_workflow
  sicnu_workflow_runtime
  sicnu_data
  sicnu_processing
  sicnu_task_center
  sicnu_jobs
)
if(TARGET jsoncpp)
  target_link_libraries(test_workflow_run_coordinator PRIVATE jsoncpp)
endif()
target_include_directories(test_workflow_run_coordinator PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_workflow_run_coordinator)

# SIGSTOP/SIGCONT process-freezing race fixture is POSIX-only.
if(NOT WIN32)
add_executable(test_workflow_cache_e2e test_workflow_cache_e2e.cpp)
target_link_libraries(test_workflow_cache_e2e PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Sicnu::data
  sicnu_processing
  sicnu_task_center
  sicnu_jobs
  sicnu_workflow
  sicnu_operators
  GDAL::GDAL
)
target_include_directories(test_workflow_cache_e2e PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
target_compile_definitions(test_workflow_cache_e2e PRIVATE
  SICNU_CLI_BINARY="${CMAKE_BINARY_DIR}/sicnu_geo_rs_cli"
)
sicnu_link_jsoncpp(test_workflow_cache_e2e)
sicnu_discover_tests(test_workflow_cache_e2e)
endif()

add_executable(test_workflow_artifact_gc test_workflow_artifact_gc.cpp)
target_link_libraries(test_workflow_artifact_gc PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  sicnu_workflow
  sicnu_workflow_runtime
  sicnu_processing
  sicnu_data
  sicnu_task_center
  sicnu_jobs
)
if(TARGET jsoncpp)
  target_link_libraries(test_workflow_artifact_gc PRIVATE jsoncpp)
endif()
target_include_directories(test_workflow_artifact_gc PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_workflow_artifact_gc)

# Milestone 2 Batch A Dialogs Empirical Challenge Suite
sicnu_add_dialog_test(test_m2_batch_a_dialogs
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/atmospheric_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/radiometric_calibration_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/contrast_stretch_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/spatial_filter_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/speckle_filter_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/spectral_index_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/spectral_library_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/band_ratio_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/band_math_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/extract_band_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/qa_mask_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/async_gdal_runner.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/async_runner_base.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/band_role_combo.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/histogram_stretch_widget.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/histogram_widget.cpp
  ${CMAKE_SOURCE_DIR}/src/app/display/display_stretch_types.cpp
  ${CMAKE_SOURCE_DIR}/src/app/display/display_stretch_resolve.cpp
  ${CMAKE_SOURCE_DIR}/src/app/display/display_stretch_apply.cpp
  ${CMAKE_SOURCE_DIR}/src/app/display/piecewise_linear_enhancement.cpp
  ${CMAKE_SOURCE_DIR}/src/app/display/qgs_display_stretch.cpp
  EXTRA_LIBS GDAL::GDAL
)

# Milestone 2 Batch B Dialogs Empirical Challenge Suite
sicnu_add_dialog_test(test_m2_batch_b_dialogs
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/orthorectification_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/mosaic_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/fusion_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/change_detection_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/pca_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/post_classification_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/terrain_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/apply_mask_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/batch_processing_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/product_import_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/crs_preset_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/crs_presets.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/comparison_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/help_viewer_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/preferences_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/classification/rs_merge_classes_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/classification/rs_post_process_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/georeferencer/rs_sift_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/georeferencer/rs_template_match_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/crs_selector.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/comparison_widget.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/band_role_combo.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/sicnu_algorithm_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/stac_browser_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/stac_client.cpp
  ${CMAKE_SOURCE_DIR}/src/app/classification/rs_accuracy_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/classification/rs_accuracy_panel.cpp
  ${CMAKE_SOURCE_DIR}/src/app/classification/rs_classifier_load_dialog.cpp
  EXTRA_LIBS GDAL::GDAL Qt6::Network qgis_analysis
)

# Milestone 3 RsEmptyStateWidget Unit Tests
add_executable(test_rs_empty_state_widget
  test_rs_empty_state_widget.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/rs_empty_state_widget.cpp
)
target_link_libraries(test_rs_empty_state_widget PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
)
target_include_directories(test_rs_empty_state_widget PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_rs_empty_state_widget PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_rs_empty_state_widget)

# Milestone 3 Empirical Challenger Stress Suite
add_executable(test_m3_stress
  test_m3_stress.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/rs_empty_state_widget.cpp
  ${CMAKE_SOURCE_DIR}/src/app/log_panel.cpp
  ${CMAKE_SOURCE_DIR}/src/app/panels/data_manager_panel.cpp
  ${CMAKE_SOURCE_DIR}/src/app/shell/gui_job_adapter.cpp
  ${CMAKE_SOURCE_DIR}/src/app/shell/processing_job_adapter.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/dialog_help_catalog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/dialog_utils.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/temporal_analysis_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/raster_processing_dialog_base.cpp
)
target_link_libraries(test_m3_stress PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
  qgis_core
  qgis_gui
  Sicnu::qgis_display
  sicnu_processing
  sicnu_task_center
  sicnu_data
  sicnu_jobs
  sicnu_operators
  GDAL::GDAL
)
target_include_directories(test_m3_stress PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_m3_stress PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_m3_stress)




# Milestone 3 Challenger 2 Adversarial Stress Suite
add_executable(test_m3_adversarial_challenge
  test_m3_adversarial_challenge.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/rs_toolbar_flow_host.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/rs_empty_state_widget.cpp
  ${CMAKE_SOURCE_DIR}/src/app/log_panel.cpp
  ${CMAKE_SOURCE_DIR}/src/app/shell/gui_job_adapter.cpp
  ${CMAKE_SOURCE_DIR}/src/app/shell/processing_job_adapter.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/dialog_help_catalog.cpp
)
target_link_libraries(test_m3_adversarial_challenge PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
  qgis_core
  qgis_gui
  Sicnu::qgis_display
  sicnu_processing
  sicnu_task_center
  sicnu_data
  sicnu_jobs
  sicnu_operators
  GDAL::GDAL
)
target_include_directories(test_m3_adversarial_challenge PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
target_compile_definitions(test_m3_adversarial_challenge PRIVATE
  CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
set_target_properties(test_m3_adversarial_challenge PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_m3_adversarial_challenge)

# Final Challenger 1 Verification Suite
add_executable(test_final_challenge
  test_final_challenge.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/rs_empty_state_widget.cpp
  ${CMAKE_SOURCE_DIR}/src/app/log_panel.cpp
)
target_link_libraries(test_final_challenge PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
  qgis_core
  qgis_gui
)
target_include_directories(test_final_challenge PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_final_challenge PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_final_challenge)

# Final Challenger 2 Empirical Verification Suite
add_executable(test_final_challenger_2_empirical
  test_final_challenger_2_empirical.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/rs_toolbar_flow_host.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/rs_empty_state_widget.cpp
  ${CMAKE_SOURCE_DIR}/src/app/shell/gui_job_adapter.cpp
  ${CMAKE_SOURCE_DIR}/src/app/shell/processing_job_adapter.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/preferences_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/dialog_help_catalog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/dialog_utils.cpp
)
target_link_libraries(test_final_challenger_2_empirical PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
  qgis_core
  qgis_gui
  Sicnu::qgis_display
  sicnu_processing
  sicnu_task_center
  sicnu_data
  sicnu_jobs
  sicnu_operators
  GDAL::GDAL
)
target_include_directories(test_final_challenger_2_empirical PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_final_challenger_2_empirical PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_final_challenger_2_empirical)

###############################################################################
# ExpRS Developer Platform 3.0 — plugin/SDK/CLI test matrix
###############################################################################
add_library(hello_plugin MODULE fixtures/hello_plugin/hello_plugin.cpp)
target_include_directories(hello_plugin PRIVATE
  ${CMAKE_SOURCE_DIR}/src/sdk
  ${CMAKE_SOURCE_DIR}/src
)
# RSOperator's out-of-line virtuals live in the shared sicnu_operators target;
# Windows MODULE links must resolve them (Linux allows undefined).
target_link_libraries(hello_plugin PRIVATE sicnu_operators sicnu_operators_core jsoncpp)
set_target_properties(hello_plugin PROPERTIES
  LIBRARY_OUTPUT_NAME hello_plugin
  PREFIX "lib"
  LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugin_fixtures/hello_plugin
)

# Isolation runtime 5.0: misbehaving fixture plugin (crash/hang/flood) for
# the host-process suites + the out-of-process plugin host test.
add_library(isolation_plugin MODULE fixtures/isolation_plugin/isolation_plugin.cpp)
target_include_directories(isolation_plugin PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/sdk
)
target_link_libraries(isolation_plugin PRIVATE sicnu_operators sicnu_operators_core jsoncpp)
set_target_properties(isolation_plugin PROPERTIES
  LIBRARY_OUTPUT_NAME isolation_plugin
  PREFIX "lib"
  LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugin_fixtures/isolation_plugin
)

sicnu_add_sdk_test(test_plugin_host_process)
target_link_libraries(test_plugin_host_process PRIVATE sicnu_plugins_hostprocess sicnu_operators_core)
target_compile_definitions(test_plugin_host_process PRIVATE
  SICNU_TEST_ISOLATION_PLUGIN_DIR="${CMAKE_BINARY_DIR}/plugin_fixtures/isolation_plugin"
  SICNU_TEST_PLUGIN_HOST_WORKER="${CMAKE_BINARY_DIR}/exprs_plugin_host_worker.exe"
)

sicnu_add_test(test_plugin_manifest)
target_link_libraries(test_plugin_manifest PRIVATE sicnu_sdk)

# Plugin packaging e2e uses rm -rf /tmp, symlink(2) and /etc — POSIX-only.
if(NOT WIN32)
sicnu_add_test(test_exprs_plugin_system)
target_link_libraries(test_exprs_plugin_system PRIVATE sicnu_sdk)
endif()

sicnu_add_test(test_exprs_plugin_loader)
target_link_libraries(test_exprs_plugin_loader PRIVATE sicnu_sdk)
target_compile_definitions(test_exprs_plugin_loader PRIVATE
  SICNU_TEST_HELLO_PLUGIN_DIR="${CMAKE_BINARY_DIR}/plugin_fixtures/hello_plugin"
)

# POSIX-only test (setenv + /tmp path semantics); the Windows lane has no
# equivalent coverage today. See test_help_* for the help-system suites.
if(NOT WIN32)
  sicnu_add_test(test_exprs_external_process)
  target_link_libraries(test_exprs_external_process PRIVATE sicnu_sdk)
endif()
# Isolation runtime 5.0: behavior helper + Windows external-process lane
# (CreateProcessW implementation). The helper is cross-platform so the same
# assertions can run on any lane.
add_executable(exprs_ep_helper helper_external_process.cpp)
set_target_properties(exprs_ep_helper PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
if(WIN32)
  sicnu_add_sdk_test(test_exprs_external_process_win)
  target_compile_definitions(test_exprs_external_process_win PRIVATE
    SICNU_TEST_EP_HELPER="${CMAKE_BINARY_DIR}/exprs_ep_helper.exe"
  )
endif()

# Isolation runtime 5.0: host-process protocol contract suites (sdk-only).
sicnu_add_sdk_test(test_exprs_ipc)
sicnu_add_sdk_test(test_plugin_capabilities)

sicnu_add_test(test_exprs_workflow_schema)
target_link_libraries(test_exprs_workflow_schema PRIVATE sicnu_sdk)

# popen()/WEXITSTATUS CLI-spawn fixture is POSIX-only.
if(NOT WIN32)
sicnu_add_test(test_cli_commands_json)
target_link_libraries(test_cli_commands_json PRIVATE sicnu_sdk sicnu_plugins_framework)
target_compile_definitions(test_cli_commands_json PRIVATE
  SICNU_TEST_CLI="$<TARGET_FILE:sicnu_geo_rs_cli>"
)
endif()

# The plugins host runtime: registry bridging + external tool operator.
sicnu_add_test(test_plugins_runtime_host)
target_link_libraries(test_plugins_runtime_host PRIVATE sicnu_plugins_framework sicnu_sdk sicnu_workflow_runtime)

# Owner-scoped unload barrier (#747).
sicnu_add_test(test_plugin_execution_barrier)
target_link_libraries(test_plugin_execution_barrier PRIVATE sicnu_plugins_framework sicnu_sdk)


# — Harness 4.0 (Pi Spatial Scientist & Agent Harness) —
# Error taxonomy (Phase 12) and canonical tool manifest/taxonomy (Phases 1/2).
sicnu_add_test(test_harness_error)
sicnu_link_jsoncpp(test_harness_error)

sicnu_add_test(test_harness_catalog)
sicnu_link_jsoncpp(test_harness_catalog)


sicnu_add_test(test_harness_grounding)
sicnu_link_jsoncpp(test_harness_grounding)


# Harness 4.0 evals: six deterministic RS scenarios + anti-hallucination +
# token budgets (mission Phases 18-20).
sicnu_add_test(test_harness_evals)
sicnu_link_jsoncpp(test_harness_evals)


# Harness 7.0 capability-knowledge drift guard: entries vs the live operator
# registry, operator x-rs-contract facts, and the closed intent vocabulary.
sicnu_add_test(test_capability_drift)
sicnu_link_jsoncpp(test_capability_drift)


# Last TEST_INCLUDE_FILES entry: stamp PYTHONHOME / LD_LIBRARY_PATH / QT_IM_MODULE
# onto every test after Catch PRE_TEST discovery has registered them (#730).
sicnu_register_test_env_applier()



# Geospatial I/O Foundation 4.0: Qt's deferred finalizer runs qmlimportscanner at
# configure time for every executable. The test suite embeds no QML, and on the
# Windows dev/CI hosts the scanner tool can fail to initialize (0xc0000139)
# depending on DLL search-order pollution, breaking configure. No test imports
# QML, so skip the scan for every test executable (property must be set before
# the directory's deferred finalizer runs; this file's end is that point).
get_property(_sicnu_test_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(_sicnu_test_target IN LISTS _sicnu_test_targets)
  get_target_property(_sicnu_test_target_type ${_sicnu_test_target} TYPE)
  if(_sicnu_test_target_type STREQUAL "EXECUTABLE")
    set_target_properties(${_sicnu_test_target} PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN TRUE)
  endif()
endforeach()

# ── Unified Help System 6.0 ────────────────────────────────────────────────
# Knowledge-layer tests: Qt Core + jsoncpp only (no GUI/qgis linkage), so the
# suite builds and runs fast while guarding the bounded-resource contracts.
# NOTE: help_content.qrc is compiled into the *executable* — RCC initializers
# inside static libraries are discarded by the linker.
function(sicnu_add_help_test NAME)
  qt_add_executable(${NAME} ${NAME}.cpp
    ${CMAKE_SOURCE_DIR}/data/help/help_content.qrc
  )
  target_link_libraries(${NAME} PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    Sicnu::help
  )
  target_include_directories(${NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
  )
  set_target_properties(${NAME} PROPERTIES AUTOMOC ON)
  sicnu_discover_tests(${NAME})
endfunction()

sicnu_add_help_test(test_help_core)

# Coverage & drift contracts against the LIVE operator registry + harness
# taxonomy: reuses the standard heavy test helper (sicnu_operators +
# sicnu_agent + processing) because the whole point is real schemas/codes.
sicnu_add_test(test_help_coverage)
target_sources(test_help_coverage PRIVATE
  # availability fact adapter pulls ContextRules, whose implementation lives
  # in the shell sources — compile the trio here as well (workbench host
  # satisfies selection_context's dependency chain)
  ${CMAKE_SOURCE_DIR}/src/app/workbench/selection_context.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/command_registry.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/workbench_host.cpp
  # embed the help knowledge content (RCC init must live in the executable)
  ${CMAKE_SOURCE_DIR}/data/help/help_content.qrc
)
target_include_directories(test_help_coverage PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
)
target_compile_definitions(test_help_coverage PRIVATE
    CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
target_link_libraries(test_help_coverage PRIVATE Sicnu::help Sicnu::help_adapters Sicnu::help_gui)

# =============================================================================
# Verification / Observability / Performance Engineering 7.0
# (feat/verification-observability-7) — appended at end of file on purpose:
# this track and feat/cloud-geospatial-io-7 both edit this file; appending
# keeps integration conflicts trivial.
# =============================================================================

# Unified trace contract (task F): ids, NDJSON encoding, ring/file sinks.
sicnu_add_test(test_trace_contract TIMEOUT 120)
target_link_libraries(test_trace_contract PRIVATE sicnu_runtime)

# Fault-injection registry semantics (task B): the registry lives in
# sicnu_runtime; production seams probe it through SICNU_FAULT_POINT and the
# fast path is one relaxed atomic load when nothing is armed.
sicnu_add_test(test_fault_registry TIMEOUT 120)
target_link_libraries(test_fault_registry PRIVATE sicnu_runtime)

# Portable fault matrix (task B): OutputCommitter / ArtifactObjectPool /
# WorkflowCheckpointManager failure paths via the fault registry. Runs on
# Windows/MSVC where the POSIX-only test_fault_injection cannot.
sicnu_add_test(test_fault_matrix TIMEOUT 300)
target_link_libraries(test_fault_matrix PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  sicnu_processing
  sicnu_data
  sicnu_workflow
  sicnu_runtime
)

# Bounded contract fuzz (task D): ResourceUri total-function/credential/
# traversal properties. Deterministic seeds, hard input caps.
sicnu_add_test(test_contract_fuzz_io TIMEOUT 180)
target_link_libraries(test_contract_fuzz_io PRIVATE Sicnu::Geospatial)
target_include_directories(test_contract_fuzz_io PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Bounded contract fuzz (task D): MapSpec condition AST + workflow placeholder
# grammar. Compiles the two Qt-free/light production sources directly so the
# fuzz target needs no sicnu_agent/sicnu_workflow closure.
add_executable(test_contract_fuzz_lang
  test_contract_fuzz_lang.cpp
  ${CMAKE_SOURCE_DIR}/src/agent/mapspec/mapspec_conditions.cpp
  ${CMAKE_SOURCE_DIR}/src/workflow/placeholder_grammar.cpp
)
target_link_libraries(test_contract_fuzz_lang PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
)
if(TARGET jsoncpp)
  target_link_libraries(test_contract_fuzz_lang PRIVATE jsoncpp)
elseif(JSONCPP_LIBRARIES)
  target_include_directories(test_contract_fuzz_lang SYSTEM PRIVATE ${JSONCPP_INCLUDE_DIRS})
  target_link_libraries(test_contract_fuzz_lang PRIVATE ${JSONCPP_LIBRARIES})
else()
  target_link_libraries(test_contract_fuzz_lang PRIVATE jsoncpp)
endif()
target_include_directories(test_contract_fuzz_lang PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/processing
  ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_definitions(test_contract_fuzz_lang PRIVATE
  CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
sicnu_discover_tests(test_contract_fuzz_lang)

# JobEngine concurrency/lifecycle stress (task E). Links the Qt-free static
# sicnu_jobs (which PUBLIC-links sicnu_operators for the registry seam).
# Deterministic condition-wait synchronization, no sleeps. RUN_SERIAL:
# saturates all workers; keep timing measurements uncontended.
add_executable(test_concurrency_stress
  test_concurrency_stress.cpp
)
target_link_libraries(test_concurrency_stress PRIVATE
  Catch2::Catch2WithMain
  sicnu_jobs
  sicnu_runtime
)
if(TARGET jsoncpp)
  target_link_libraries(test_concurrency_stress PRIVATE jsoncpp)
elseif(JSONCPP_LIBRARIES)
  target_include_directories(test_concurrency_stress SYSTEM PRIVATE ${JSONCPP_INCLUDE_DIRS})
  target_link_libraries(test_concurrency_stress PRIVATE ${JSONCPP_LIBRARIES})
else()
  target_link_libraries(test_concurrency_stress PRIVATE jsoncpp)
endif()
target_include_directories(test_concurrency_stress PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/processing
  ${CMAKE_CURRENT_SOURCE_DIR}
)
sicnu_discover_tests(test_concurrency_stress TIMEOUT 600 RUN_SERIAL TRUE)

# Scientific known-answer corpus (task C): analytically derivable fixtures for
# spectral / change / terrain / radiometric / transition-matrix / temporal
# statistics. Physical invariants with documented derivations, not shapes.
sicnu_add_test(test_known_answer_corpus TIMEOUT 180)
target_link_libraries(test_known_answer_corpus PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  sicnu_processing
  sicnu_operators_core
)

# Machine-readable diagnostic envelope (task G): exp.diag.v1 contract.
sicnu_add_test(test_diagnostic_report TIMEOUT 60)
target_link_libraries(test_diagnostic_report PRIVATE sicnu_runtime)

# Quality-platform micro-baselines (task H) — run MANUALLY, never a ctest
# gate: benchmark_quality7 --out benchmarks/quality7.json
add_executable(benchmark_quality7
  benchmark_quality7.cpp
  ${CMAKE_SOURCE_DIR}/src/agent/mapspec/mapspec_conditions.cpp
)
target_link_libraries(benchmark_quality7 PRIVATE
  Qt6::Core
  GDAL::GDAL
  sicnu_jobs
  sicnu_operators_core
  sicnu_geospatial
  sicnu_runtime
)
if(TARGET jsoncpp)
  target_link_libraries(benchmark_quality7 PRIVATE jsoncpp)
elseif(JSONCPP_LIBRARIES)
  target_include_directories(benchmark_quality7 SYSTEM PRIVATE ${JSONCPP_INCLUDE_DIRS})
  target_link_libraries(benchmark_quality7 PRIVATE ${JSONCPP_LIBRARIES})
else()
  target_link_libraries(benchmark_quality7 PRIVATE jsoncpp)
endif()
target_include_directories(benchmark_quality7 PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/processing
  ${CMAKE_CURRENT_SOURCE_DIR}
)

# Bounded contract fuzz (task D): DatasetManifest unknown-field tolerance,
# total parse on mutated payloads, typed negative cases. Light closure
# (Qt + SQLite only).
sicnu_add_test(test_contract_fuzz_data TIMEOUT 180)
target_link_libraries(test_contract_fuzz_data PRIVATE
  Sicnu::dataset
  SQLite::SQLite3
)
target_include_directories(test_contract_fuzz_data PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Bounded contract fuzz (task D): AgentPlan reader + MapSpec validate/patch.
# Heavy closure (sicnu_agent) — part of the full-tree lane.
sicnu_add_test(test_contract_fuzz_agent TIMEOUT 300)
target_link_libraries(test_contract_fuzz_agent PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  sicnu_data
  sicnu_agent
  sicnu_operators
  sicnu_workflow
  sicnu_processing
  sicnu_task_center
)
target_include_directories(test_contract_fuzz_agent PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/processing
  ${CMAKE_CURRENT_SOURCE_DIR}
)
