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
  # Optional TEST_PREFIX (D15): forwarded to catch_discover_tests so track
  # gates can select cases by binary name (`ctest -R "test_xxx::"`).
  set(_sat_prefix "")
  cmake_parse_arguments(SDT "" "TEST_PREFIX" "" ${ARGN})
  if(SDT_TEST_PREFIX)
    set(_sat_prefix TEST_PREFIX ${SDT_TEST_PREFIX})
  endif()
  set(_discovery_props "")
  if(ENABLE_SANITIZERS)
    list(APPEND _discovery_props "ENVIRONMENT_MODIFICATION" "LSAN_OPTIONS=set:detect_leaks=0")
  endif()
  if(SDT_UNPARSED_ARGUMENTS)
    # Remaining args are a flat list of key/value test properties
    # (RUN_SERIAL TRUE, ...) — unchanged behavior for existing callers.
    list(APPEND _discovery_props ${SDT_UNPARSED_ARGUMENTS})
  endif()
  set(_dl_paths "")
  if(SICNU_TEST_DL_PATHS)
    set(_dl_paths DL_PATHS ${SICNU_TEST_DL_PATHS})
  endif()
  # PRE_TEST avoids race conditions in parallel builds where discovery
  # tries to run the test executable before it is linked.
  catch_discover_tests(${NAME}
    DISCOVERY_MODE PRE_TEST
    ${_sat_prefix}
    ${_dl_paths}
    PROPERTIES ${_discovery_props})
endfunction()

# Helper function to add a test executable. Extra args (TIMEOUT n,
# RUN_SERIAL TRUE, ...) are forwarded to sicnu_discover_tests as test
# properties — previously they were accepted and silently dropped.
function(sicnu_add_test NAME)
  cmake_parse_arguments(SAT "" "TEST_PREFIX" "" ${ARGN})
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
  if(SAT_TEST_PREFIX)
    if(SAT_UNPARSED_ARGUMENTS)
      sicnu_discover_tests(${NAME} TEST_PREFIX ${SAT_TEST_PREFIX} ${SAT_UNPARSED_ARGUMENTS})
    else()
      sicnu_discover_tests(${NAME} TEST_PREFIX ${SAT_TEST_PREFIX})
    endif()
  elseif(SAT_UNPARSED_ARGUMENTS)
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

# RS14-07 Parameter Studio: light study test executables. Links only
# Catch2 + Sicnu::study (Qt Core + experiment authority transitively) —
# no QGIS, no GUI — so the study contract suites build and run fast offline.
function(sicnu_add_study_test NAME)
  add_executable(${NAME} ${NAME}.cpp)
  target_link_libraries(${NAME} PRIVATE
    Catch2::Catch2WithMain
    Sicnu::study
  )
  target_include_directories(${NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
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

# Lab Sample Data Foundry (D1): light fixture suite — links only Catch2 + the
# foundry (GDAL + jsoncpp transitively), no Qt/QGIS. Fixtures are generated at
# runtime into temp dirs (repo convention: no tracked rasters). The CLI
# subprocess path is pinned via the generator target's file location.
add_executable(test_sample_fixtures test_sample_fixtures.cpp)
target_link_libraries(test_sample_fixtures PRIVATE
  Catch2::Catch2WithMain
  sicnu_sample_foundry
)
target_include_directories(test_sample_fixtures PRIVATE
  ${CMAKE_SOURCE_DIR}/tools
  ${CMAKE_BINARY_DIR}
)
target_compile_definitions(test_sample_fixtures PRIVATE
  CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
  SICNU_GENERATE_SAMPLES_BIN="$<TARGET_FILE:sicnu_generate_samples>"
)
# The genex above carries no build dependency; make the subprocess binary
# rebuild whenever the suite is built.
add_dependencies(test_sample_fixtures sicnu_generate_samples)
set_target_properties(test_sample_fixtures PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
# Discover through the same properties sicnu_discover_tests applies (GDAL
# needs DL_PATHS in conda lanes, #730; sanitizer lanes need LSAN leak
# suppression), plus a TEST_PREFIX so `ctest -R sample_fixtures` selects the
# suite: Catch discovery names entries after the English TEST_CASE titles.
set(_sf_discovery_props "")
if(ENABLE_SANITIZERS)
  list(APPEND _sf_discovery_props "ENVIRONMENT_MODIFICATION" "LSAN_OPTIONS=set:detect_leaks=0")
endif()
set(_sf_dl_paths "")
if(SICNU_TEST_DL_PATHS)
  set(_sf_dl_paths DL_PATHS ${SICNU_TEST_DL_PATHS})
endif()
catch_discover_tests(test_sample_fixtures
  DISCOVERY_MODE PRE_TEST
  TEST_PREFIX "sample_fixtures: "
  ${_sf_dl_paths}
  PROPERTIES ${_sf_discovery_props}
)


# Geospatial I/O Foundation 4.0 (light targets — see sicnu_add_io_test above).
sicnu_add_io_test(test_io_canonical_metadata)

# RS14-07 Parameter Sensitivity & Uncertainty Studio (light study targets).
sicnu_add_study_test(test_study_spec)
sicnu_add_study_test(test_study_sampling)
sicnu_add_study_test(test_study_runner)
sicnu_add_study_test(test_study_analysis)
sicnu_add_study_test(test_study_spatial)
sicnu_add_study_test(test_study_export)

# Experiment Exploration Studio projections (Catch2 + Sicnu::experiment_studio).
# Qt Core only — no QGIS/GUI so suites stay offline-fast like study tests.
function(sicnu_add_experiment_studio_test NAME)
  add_executable(${NAME} ${NAME}.cpp)
  target_link_libraries(${NAME} PRIVATE
    Catch2::Catch2WithMain
    Sicnu::experiment_studio
    Sicnu::study
  )
  target_include_directories(${NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
  )
  target_compile_definitions(${NAME} PRIVATE
    SICNU_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
  )
  set_target_properties(${NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  sicnu_discover_tests(${NAME})
endfunction()

sicnu_add_experiment_studio_test(test_experiment_studio_core)

target_link_libraries(test_study_spatial PRIVATE GDAL::GDAL)
# Shipped exemplar specs: loaded through the production reader, so the test
# needs the source tree path (light target — no QGIS).
sicnu_add_study_test(test_study_exemplars)
target_compile_definitions(test_study_exemplars PRIVATE
  CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
# RS14-07 full-stack teaching e2e: the NDVI threshold exemplar runs through the
# REAL spine (ExecutionPlane → TaskCenter → JobEngine → rs:threshold_raster).
# Heavy by design — sicnu_add_test links the qgis/processing/agent chain; build
# it once and run it before final review (see .planning/RS14-07 build notes).
sicnu_add_test(test_study_e2e)
target_link_libraries(test_study_e2e PRIVATE
  Sicnu::study
  Sicnu::study_bridge
  GDAL::GDAL
)
# Studio live-execution closed loop: fake + real-shaped backend over the
# studio_live bridge, then the real spine E2E (study → spatial → divergence →
# fault sandbox → capsule reload → refs-only export). Heavy for the same
# reason as test_study_e2e.
sicnu_add_test(test_studio_live_e2e)
target_link_libraries(test_studio_live_e2e PRIVATE
  Sicnu::experiment_studio_live
  GDAL::GDAL
  SQLite::SQLite3
)
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
# 8.0: cacheability/reproducibility doctor checks probe remote identities
# over the shared loopback fixture.
target_sources(test_io_doctor PRIVATE support/http_range_server.cpp)
if(WIN32)
  target_link_libraries(test_io_doctor PRIVATE ws2_32)
endif()
sicnu_add_io_test(test_io_atomic_failures)
sicnu_add_io_test(test_io_fidelity)
sicnu_add_io_test(test_io_paths)
sicnu_add_io_test(test_io_uri)

# Cross-platform Completion: header-only portability primitives
# (src/platform/portable.h). Lightest possible lane — Catch2 only, no sicnu
# library, no Qt/QGIS/GDAL — so the UTF-8/path/pid contract compiles and
# runs on every platform lane.
add_executable(test_platform_portability test_platform_portability.cpp)
target_link_libraries(test_platform_portability PRIVATE Catch2::Catch2WithMain)
target_include_directories(test_platform_portability PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_platform_portability PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
sicnu_discover_tests(test_platform_portability)

# Offline classroom deployment (goal D7): the offline gate refuses remote
# requests without any network attempt — typed error + GDAL cloud deny.
sicnu_add_test(test_offline_mode TIMEOUT 300)
sicnu_add_io_test(test_io_product_registry)
sicnu_add_io_test(test_io_probe)
sicnu_add_io_test(test_adversarial_m3)

# Remote range harness (ADR 0139): local HTTP fixture with byte accounting.
# ADR 0146: exit 77 = skipped (matches the ladder's verdict mapping).
sicnu_add_io_test(test_io_remote_range SKIP_RETURN_CODE 77)
target_sources(test_io_remote_range PRIVATE support/http_range_server.cpp)
if(WIN32)
  target_link_libraries(test_io_remote_range PRIVATE ws2_32)
endif()

# 7.0 remote source identity & validators (shared HTTP fixture).
# ADR 0146: exit 77 = skipped (matches the ladder's verdict mapping).
sicnu_add_io_test(test_io_remote_validator SKIP_RETURN_CODE 77)
target_sources(test_io_remote_validator PRIVATE support/http_range_server.cpp)
if(WIN32)
  target_link_libraries(test_io_remote_validator PRIVATE ws2_32)
endif()

# 7.0 bounded byte-range cache over remote rasters.
# ADR 0146: exit 77 = skipped (matches the ladder's verdict mapping).
sicnu_add_io_test(test_io_range_cache SKIP_RETURN_CODE 77)
target_sources(test_io_range_cache PRIVATE support/http_range_server.cpp)
if(WIN32)
  target_link_libraries(test_io_range_cache PRIVATE ws2_32)

  # #1055 compile-time portability guard: the Windows branch of the disk block
  # store must not fall back to the deprecated POSIX ::getpid() alias (only
  # visible on MSVC when <process.h> arrives transitively). Suppressing UCRT
  # non-STDC names turns any such use into a hard error; the object is built as
  # a dependency of the test target and never linked.
  add_library(sicnu_range_cache_disk_pid_guard OBJECT
    ${CMAKE_SOURCE_DIR}/src/geospatial/remote/range_cache_disk.cpp)
  target_compile_definitions(sicnu_range_cache_disk_pid_guard PRIVATE _CRT_DECLARE_NONSTDC_NAMES=0)
  target_link_libraries(sicnu_range_cache_disk_pid_guard PRIVATE Sicnu::Geospatial)
  add_dependencies(test_io_range_cache sicnu_range_cache_disk_pid_guard)
endif()

# 7.0 STAC API client (loopback JSON fixture).
sicnu_add_io_test(test_io_stac_client)
target_sources(test_io_stac_client PRIVATE support/http_stac_server.cpp support/http_range_server.cpp)
if(WIN32)
  target_link_libraries(test_io_stac_client PRIVATE ws2_32)
endif()

# 8.0 identity primitives: SHA-256 vectors, ISO-8601 normalization and
# fail-closed remote identity tokens (shared HTTP fixture).
sicnu_add_io_test(test_io_identity)
target_sources(test_io_identity PRIVATE support/http_range_server.cpp)
if(WIN32)
  target_link_libraries(test_io_identity PRIVATE ws2_32)
endif()

# 7.0 modern vector interop audit (field types / null / collections / CRS /
# streaming / driver-gated GeoParquet).
sicnu_add_io_test(test_io_vector_interop)

# 7.0 generalized grid classification & placement (rotated/flipped/GCP/RPC).
sicnu_add_io_test(test_io_grid_descriptor)

# 11.0 geospatial I/O interchange: boundary path guards, finalize manifest
# (provenance digest sidecar), dataset staging ledger (attach/sweep).
sicnu_add_io_test(test_io_param_guard)
sicnu_add_io_test(test_io_finalize_manifest)
sicnu_add_io_test(test_io_stage_ledger)
# 11.0 explicit COG production options (blocksize/overviews/determinism).
sicnu_add_io_test(test_io_cog_options)
# 11.0 capability-gated vector interchange, subdataset inventory, metadata
# write-back (validated, read-back verified, manifest continuity).
sicnu_add_io_test(test_io_vector_interchange)
sicnu_add_io_test(test_io_subdataset_inventory)
find_library(NETCDF_LIBRARY NAMES netcdf)
target_link_libraries(test_io_subdataset_inventory PRIVATE ${NETCDF_LIBRARY})
sicnu_add_io_test(test_io_metadata_patch)
# 11.0 GDAL version/capability matrix: feature detection, corrupt/truncated
# negatives, huge logical datasets within bounded memory.
sicnu_add_io_test(test_io_gdal_matrix)

# io:* operator family suite — rides the operator chain (Qt + sicnu_operators).
sicnu_add_io_test(test_io_operators)
sicnu_add_io_test(test_io_catalog_query)
sicnu_add_io_test(test_io_hints)
sicnu_add_io_test(test_io_scale9)
target_link_libraries(test_io_operators PRIVATE Qt6::Core sicnu_operators)
sicnu_link_jsoncpp(test_io_operators)

# fabric-10: data fabric / data cube suites. The object-store suite runs a
# REAL /vsis3/ open against a loopback S3-shaped endpoint (header-only
# fixture); both suites need the tests/ include for shared fixtures.
sicnu_add_io_test(test_io_fabric_object_store)
target_include_directories(test_io_fabric_object_store PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(WIN32)
  target_link_libraries(test_io_fabric_object_store PRIVATE ws2_32)
endif()
# fabric-11 (cloud-data-fabric-11): canonical object identity + VSI-object
# range cache + offline mirror replay. Same loopback-S3 fixture shape.
sicnu_add_io_test(test_io_fabric_identity_11)
target_include_directories(test_io_fabric_identity_11 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(WIN32)
  target_link_libraries(test_io_fabric_identity_11 PRIVATE ws2_32)
endif()
sicnu_add_io_test(test_io_fabric_replay_11)
target_include_directories(test_io_fabric_replay_11 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(WIN32)
  target_link_libraries(test_io_fabric_replay_11 PRIVATE ws2_32)
endif()
# 11.0 multidim planning/execution (netCDF-authored oracle, driver-gated).
sicnu_add_io_test(test_io_fabric_multidim_11)
target_include_directories(test_io_fabric_multidim_11 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
find_library(NETCDF_LIBRARY_11 NAMES netcdf)
target_link_libraries(test_io_fabric_multidim_11 PRIVATE ${NETCDF_LIBRARY_11})
# 11.0 access-pattern-driven prefetch (WP F).
sicnu_add_io_test(test_io_fabric_locality_11)
target_include_directories(test_io_fabric_locality_11 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_sources(test_io_fabric_locality_11 PRIVATE support/http_range_server.cpp)
if(WIN32)
  target_link_libraries(test_io_fabric_locality_11 PRIVATE ws2_32)
endif()
sicnu_add_io_test(test_io_fabric_catalog)
target_include_directories(test_io_fabric_catalog PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_sources(test_io_fabric_catalog PRIVATE support/http_stac_server.cpp support/http_range_server.cpp)
if(WIN32)
  # 11.0: the loopback fixtures need winsock explicitly (the targets relied
  # on a transitive ws2_32 in the tracks that built them before).
  target_link_libraries(test_io_fabric_catalog PRIVATE ws2_32)
endif()
sicnu_add_io_test(test_io_fabric_cube)
target_include_directories(test_io_fabric_cube PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
sicnu_add_io_test(test_io_fabric_plan)
target_include_directories(test_io_fabric_plan PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_sources(test_io_fabric_plan PRIVATE support/http_range_server.cpp)
if(WIN32)
  target_link_libraries(test_io_fabric_plan PRIVATE ws2_32)
endif()
# The operator suite rides the operator chain (Qt + sicnu_operators), same
# shape as test_io_operators.
sicnu_add_io_test(test_io_fabric_scale)
target_include_directories(test_io_fabric_scale PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
# 12.0 — offline mirror maintenance (verify/repair/prune) over plain local
# fixtures (no network, no Qt).
sicnu_add_io_test(test_io_mirror_maintenance)
target_include_directories(test_io_mirror_maintenance PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
# 12.0 — out-of-core microbench: real loopback HTTP, bytes/requests/peak
# asserted as correctness gates (latencies reported, never asserted).
sicnu_add_io_test(test_io_microbench)
target_sources(test_io_microbench PRIVATE support/http_range_server.cpp)
target_include_directories(test_io_microbench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(WIN32)
  target_link_libraries(test_io_microbench PRIVATE ws2_32)
endif()
sicnu_add_io_test(test_io_fabric_operators)
target_include_directories(test_io_fabric_operators PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_io_fabric_operators PRIVATE Qt6::Core sicnu_operators)
sicnu_link_jsoncpp(test_io_fabric_operators)
if(WIN32)
  target_link_libraries(test_io_fabric_catalog PRIVATE ws2_32)
endif()

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
  test_platform9.cpp
  test_cartography_operators_10.cpp
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

# Cartography Production 11.0: export manifest contract, produce chain,
# atlas delivery, series planning, template governance. Standalone target so
# the 11.0 surface evolves without touching the platform aggregate suites.
qt_add_executable(test_cartography_production_11
  test_cartography_production_11.cpp
)
target_compile_definitions(test_cartography_production_11 PRIVATE
  SICNU_CARTOGRAPHY_DATA_DIR="${CMAKE_SOURCE_DIR}/data/cartography"
  CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
target_link_libraries(test_cartography_production_11 PRIVATE
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
  target_link_libraries(test_cartography_production_11 PRIVATE jsoncpp)
endif()
target_include_directories(test_cartography_production_11 PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_cartography_production_11)

# D3 lab content expansion track: lab-chain integrity tests (pipeline JSON ->
# operator registry resolution, LabSpec artifact consistency, lab11 MapSpec
# validate + compliance preflight + offscreen compile). Standalone target so
# it stays green regardless of the visual-cartography aggregate's state.
qt_add_executable(test_lab_chains
  test_lab_chains.cpp
)
target_compile_definitions(test_lab_chains PRIVATE
  SICNU_CARTOGRAPHY_DATA_DIR="${CMAKE_SOURCE_DIR}/data/cartography"
  CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
target_link_libraries(test_lab_chains PRIVATE
  Catch2::Catch2
  Qt6::Core
  Qt6::Gui
  qgis_core
  sicnu_agent
  sicnu_operators
)
if(TARGET jsoncpp)
  target_link_libraries(test_lab_chains PRIVATE jsoncpp)
endif()
target_include_directories(test_lab_chains PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_lab_chains)

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
  ${CMAKE_SOURCE_DIR}/src/app/workbench/object_identity.cpp
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
sicnu_link_jsoncpp(test_provenance_section)
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

# RS14-19 Scientific Inspector view-model contract tests (ADR 0172). The
# test file shipped orphaned (no target) and never ran; this registers it
# over the same codec sources the app compiles.
add_executable(test_sci_inspector
  test_sci_inspector.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/scientific/sci_inspection.cpp
)
target_link_libraries(test_sci_inspector PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
)
target_include_directories(test_sci_inspector PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_sci_inspector PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_sci_inspector)

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

# Solar-earth geometry tests (radiometric-physics-11)
add_executable(test_solar_geometry test_solar_geometry.cpp)
target_link_libraries(test_solar_geometry PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  sicnu_processing
  sicnu_operators
  GDAL::GDAL
)
sicnu_link_jsoncpp(test_solar_geometry)
target_include_directories(test_solar_geometry PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_solar_geometry TEST_PREFIX "test_solar_geometry::")

# Radiometric transition authority tests (radiometric-physics-11)
add_executable(test_radiometric_transition test_radiometric_transition.cpp)
target_link_libraries(test_radiometric_transition PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  sicnu_processing
)
target_include_directories(test_radiometric_transition PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_radiometric_transition TEST_PREFIX "test_radiometric_transition::")

# Atmospheric provider seam tests (radiometric-physics-11)
add_executable(test_atmospheric_provider test_atmospheric_provider.cpp)
target_link_libraries(test_atmospheric_provider PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  sicnu_processing
)
target_include_directories(test_atmospheric_provider PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_atmospheric_provider TEST_PREFIX "test_atmospheric_provider::")

# BRDF normalization tests (radiometric-physics-11)
add_executable(test_brdf_normalization test_brdf_normalization.cpp)
target_link_libraries(test_brdf_normalization PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  sicnu_processing
  sicnu_operators
  GDAL::GDAL
)
sicnu_link_jsoncpp(test_brdf_normalization)
target_include_directories(test_brdf_normalization PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_brdf_normalization TEST_PREFIX "test_brdf_normalization::")

# Radiometric QA flag tests (radiometric-physics-11)
add_executable(test_radiometric_qa test_radiometric_qa.cpp)
target_link_libraries(test_radiometric_qa PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  sicnu_processing
  sicnu_operators
  GDAL::GDAL
)
sicnu_link_jsoncpp(test_radiometric_qa)
target_include_directories(test_radiometric_qa PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_radiometric_qa TEST_PREFIX "test_radiometric_qa::")

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

# Spectral table artifact + wavelength grid helpers (Hyperspectral Platform
# 10.0): digest/tamper contract, provenance/license rule, cell bound, unit
# normalization and grid validation.
add_executable(test_spectral_table test_spectral_table.cpp)
target_link_libraries(test_spectral_table PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  sicnu_processing
)
target_include_directories(test_spectral_table PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_spectral_table)

# Complete MNF chain (Hyperspectral Platform 10.0): forward/inverse known
# answers, SNR ordering, dropped-mass RMSE, singular-noise refusal, transform
# artifact digest.
add_executable(test_mnf_transform test_mnf_transform.cpp)
target_link_libraries(test_mnf_transform PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  sicnu_processing
)
target_include_directories(test_mnf_transform PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_mnf_transform)

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

# Spectral Intelligence 12.0 work package A: CEM target detector kernel —
# closed forms, streaming correlation accumulation, scaled loading,
# condition proxy, fail-closed refusals.
add_executable(test_spectral_cem test_spectral_cem.cpp)
target_link_libraries(test_spectral_cem PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  sicnu_processing
  sicnu_operators
  GDAL::GDAL
)
sicnu_link_jsoncpp(test_spectral_cem)
target_include_directories(test_spectral_cem PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_spectral_cem)

# Spectral Intelligence 12.0 work package G: detector tile-vs-whole-scene
# streaming equivalence (MF/ACE/CEM), including declared-NoData QA honesty.
add_executable(test_spectral_detection_streaming test_spectral_detection_streaming.cpp)
target_link_libraries(test_spectral_detection_streaming PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  sicnu_processing
  sicnu_operators
  GDAL::GDAL
)
sicnu_link_jsoncpp(test_spectral_detection_streaming)
target_include_directories(test_spectral_detection_streaming PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_spectral_detection_streaming)

# Spectral Intelligence 12.0 work package B: spatial consistency fusion —
# closed forms, NoData leak-proofness, tile-halo equivalence, operator E2E.
add_executable(test_spectral_spatial_fusion test_spectral_spatial_fusion.cpp)
target_link_libraries(test_spectral_spatial_fusion PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  sicnu_processing
  sicnu_operators
  GDAL::GDAL
)
sicnu_link_jsoncpp(test_spectral_spatial_fusion)
target_include_directories(test_spectral_spatial_fusion PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_spectral_spatial_fusion)

# Spectral Intelligence 12.0 work package C: library-scale matching —
# brute-force equivalence, grid-bucket resample cache, prescreened top-K,
# machine-independent scale evidence.
add_executable(test_spectral_library_scale test_spectral_library_scale.cpp)
target_link_libraries(test_spectral_library_scale PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  sicnu_processing
  GDAL::GDAL
)
sicnu_link_jsoncpp(test_spectral_library_scale)
target_include_directories(test_spectral_library_scale PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_spectral_library_scale)

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

# Spectral formula drift guard (Scientific Processing 8.0): operator vs
# independent documented formulas, schema-enum coverage, NaN contracts.
add_executable(test_spectral_formula_drift test_spectral_formula_drift.cpp)
target_link_libraries(test_spectral_formula_drift PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  sicnu_processing
  sicnu_operators
  GDAL::GDAL
)
sicnu_link_jsoncpp(test_spectral_formula_drift)
target_include_directories(test_spectral_formula_drift PRIVATE
  ${CMAKE_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_spectral_formula_drift)

# Raster-vector analytics (Scientific Processing 8.0): rs:rasterize +
# rs:zonal_stats over GeoJSON fixtures, analytic expectations.
add_executable(test_raster_vector test_raster_vector.cpp)
target_link_libraries(test_raster_vector PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  sicnu_processing
  sicnu_operators
  GDAL::GDAL
)
sicnu_link_jsoncpp(test_raster_vector)
target_include_directories(test_raster_vector PRIVATE
  ${CMAKE_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_raster_vector)

# SAR temporal statistics (Scientific Processing 8.0): linear-domain
# aggregates, robust log-change, operator E2E.
add_executable(test_sar_temporal_stats test_sar_temporal_stats.cpp)
target_link_libraries(test_sar_temporal_stats PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  sicnu_processing
  sicnu_operators
  GDAL::GDAL
)
sicnu_link_jsoncpp(test_sar_temporal_stats)
target_include_directories(test_sar_temporal_stats PRIVATE
  ${CMAKE_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_sar_temporal_stats)

# SAR forward geocoding (Scientific Processing 8.0): scene-contract parser,
# per-cell forward range-Doppler geometry, operator E2E on analytic fixtures.
add_executable(test_sar_geocoding test_sar_geocoding.cpp)
target_link_libraries(test_sar_geocoding PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  sicnu_processing
  sicnu_operators
  GDAL::GDAL
)
sicnu_link_jsoncpp(test_sar_geocoding)
target_include_directories(test_sar_geocoding PRIVATE
  ${CMAKE_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_sar_geocoding)

# Complex (SLC) raster contract (Advanced SAR 10.0, package A): channel
# declaration grammar, complex scalar kernels, CFloat32 validation, tile
# stream round-trip + halo replication + invalid normalization.
add_executable(test_sar_complex test_sar_complex.cpp)
target_link_libraries(test_sar_complex PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  sicnu_processing
  GDAL::GDAL
)
target_include_directories(test_sar_complex PRIVATE
  ${CMAKE_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_sar_complex)

# PolSAR decomposition kernels (Advanced SAR 10.0, package B): 3×3
# Hermitian eigensolver + Pauli / H-A-α / Freeman-Durden / Yamaguchi
# known answers.
add_executable(test_sar_polsar test_sar_polsar.cpp)
target_link_libraries(test_sar_polsar PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  sicnu_processing
  GDAL::GDAL
)
target_include_directories(test_sar_polsar PRIVATE
  ${CMAKE_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_sar_polsar)

# InSAR base-chain kernels (Advanced SAR 10.0, package C): interferogram,
# coherence, Goldstein filter, ramp fit, quality-guided unwrap,
# displacement, coregistration shift.
add_executable(test_sar_insar test_sar_insar.cpp)
target_link_libraries(test_sar_insar PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  sicnu_processing
  GDAL::GDAL
)
target_include_directories(test_sar_insar PRIVATE
  ${CMAKE_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_sar_insar)

# InSAR pair-level input truth (Advanced InSAR 11.0, package A): scene
# truth validation, wavelength/temporal consistency, per-ground-point
# baseline against an analytic translated-orbit oracle.
add_executable(test_sar_baseline test_sar_baseline.cpp)
target_link_libraries(test_sar_baseline PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  sicnu_processing
  GDAL::GDAL
)
target_include_directories(test_sar_baseline PRIVATE
  ${CMAKE_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_sar_baseline)

# DEM/orbit topographic phase (Advanced InSAR 11.0, package B): analytic
# equator oracle + independent scan/bisect oracle off-nadir, removal
# semantics, typed refusals.
add_executable(test_sar_topographic_phase test_sar_topographic_phase.cpp)
target_link_libraries(test_sar_topographic_phase PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  sicnu_processing
  GDAL::GDAL
)
target_include_directories(test_sar_topographic_phase PRIVATE
  ${CMAKE_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_sar_topographic_phase)

# Local offset-field co-registration (Advanced InSAR 11.0, package C):
# piecewise-shift known answers, warp reconstruction, honest degradation.
add_executable(test_sar_coregistration test_sar_coregistration.cpp)
target_link_libraries(test_sar_coregistration PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  sicnu_processing
  GDAL::GDAL
)
target_include_directories(test_sar_coregistration PRIVATE
  ${CMAKE_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_sar_coregistration)

# Phase closure QA (Advanced InSAR 11.0, package E): the algebraic
# identity closure == 0, inconsistency detection, NaN bookkeeping.
add_executable(test_sar_phase_closure test_sar_phase_closure.cpp)
target_link_libraries(test_sar_phase_closure PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  sicnu_processing
  GDAL::GDAL
)
target_include_directories(test_sar_phase_closure PRIVATE
  ${CMAKE_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_sar_phase_closure)

# External unwrap provider adapter (Advanced InSAR 11.0, package D): the
# fake provider executable mirrors the process contract; every failure
# branch is a typed refusal with no scratch left behind.
add_executable(sar_fake_unwrap_provider support/sar_fake_unwrap_provider.cpp)
target_include_directories(sar_fake_unwrap_provider PRIVATE ${CMAKE_BINARY_DIR})

add_executable(test_sar_unwrap_provider test_sar_unwrap_provider.cpp)
target_link_libraries(test_sar_unwrap_provider PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  sicnu_processing
  GDAL::GDAL
)
target_include_directories(test_sar_unwrap_provider PRIVATE
  ${CMAKE_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
target_compile_definitions(test_sar_unwrap_provider PRIVATE
  FAKE_PROVIDER_PATH="$<TARGET_FILE:sar_fake_unwrap_provider>"
)
add_dependencies(test_sar_unwrap_provider sar_fake_unwrap_provider)
sicnu_discover_tests(test_sar_unwrap_provider)

# Pair network (Advanced InSAR 11.0, package E): constraint filtering,
# connectivity, fail-closed metadata semantics.
add_executable(test_sar_pair_network test_sar_pair_network.cpp)
target_link_libraries(test_sar_pair_network PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  sicnu_processing
  GDAL::GDAL
)
target_include_directories(test_sar_pair_network PRIVATE
  ${CMAKE_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_sar_pair_network)

# Small-baseline linear network inversion (Advanced InSAR 11.0, package
# F): constructed-truth recovery, missing-data degradation, weights,
# pattern bounds.
add_executable(test_sar_network_inversion test_sar_network_inversion.cpp)
target_link_libraries(test_sar_network_inversion PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  sicnu_processing
  GDAL::GDAL
)
target_include_directories(test_sar_network_inversion PRIVATE
  ${CMAKE_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_sar_network_inversion)

# Advanced InSAR 11.0 operator E2E (package H): analytic DEM/orbit topo
# removal, external provider, pair network, network inversion, local
# co-registration through the registry.
add_executable(test_sar_platform11 test_sar_platform11.cpp)
target_link_libraries(test_sar_platform11 PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  sicnu_processing
  sicnu_operators
  GDAL::GDAL
)
target_include_directories(test_sar_platform11 PRIVATE
  ${CMAKE_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
target_compile_definitions(test_sar_platform11 PRIVATE
  FAKE_PROVIDER_PATH="$<TARGET_FILE:sar_fake_unwrap_provider>"
)
add_dependencies(test_sar_platform11 sar_fake_unwrap_provider)
sicnu_discover_tests(test_sar_platform11)

# Multi-temporal SAR event dating (Advanced SAR 10.0, package D): ISO 8601
# contract, irregular-interval day arithmetic, event kernel known answers.
add_executable(test_sar_temporal_events test_sar_temporal_events.cpp)
target_link_libraries(test_sar_temporal_events PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  sicnu_processing
  GDAL::GDAL
)
target_include_directories(test_sar_temporal_events PRIVATE
  ${CMAKE_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_sar_temporal_events)

# Advanced SAR / PolSAR / InSAR 10.0 operator E2E: PolSAR decomposition,
# full InSAR chain closure, temporal event dating, coregistration — through
# the live operator registry on synthetic fixtures.
add_executable(test_sar_platform10 test_sar_platform10.cpp)
target_link_libraries(test_sar_platform10 PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  sicnu_processing
  sicnu_operators
  GDAL::GDAL
)
sicnu_link_jsoncpp(test_sar_platform10)
target_include_directories(test_sar_platform10 PRIVATE
  ${CMAKE_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_sar_platform10)

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

# Terrain hydrology 11.0: epsilon flat resolution, D∞ flow, outlets,
# threshold/Strahler stream networks + link vectorization.
add_executable(test_terrain_hydrology test_terrain_hydrology.cpp)
target_link_libraries(test_terrain_hydrology PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  sicnu_processing
  GDAL::GDAL
)
target_include_directories(test_terrain_hydrology PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/tests
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_terrain_hydrology)

# Terrain visibility 11.0: R3 viewshed, curvature/refraction, cumulative
# observers, horizon profiles.
add_executable(test_terrain_viewshed test_terrain_viewshed.cpp)
target_link_libraries(test_terrain_viewshed PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  sicnu_processing
  GDAL::GDAL
)
target_include_directories(test_terrain_viewshed PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/tests
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_terrain_viewshed)

# Terrain solar 11.0: parallel-ray shadow duration + low-precision solar
# position (tabulated astronomy oracle).
add_executable(test_terrain_solar test_terrain_solar.cpp)
target_link_libraries(test_terrain_solar PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  sicnu_processing
  GDAL::GDAL
)
target_include_directories(test_terrain_solar PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/tests
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_terrain_solar)

# Terrain landform 11.0: multiscale TPI, Weiss classes, geomorphon patterns.
add_executable(test_terrain_landform test_terrain_landform.cpp)
target_link_libraries(test_terrain_landform PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  sicnu_processing
  GDAL::GDAL
)
target_include_directories(test_terrain_landform PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/tests
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_terrain_landform)

# Terrain analytics E2E 11.0: new products through the operator registry,
# cancellation, and the fail-closed cell budget.
add_executable(test_terrain_analytics_e2e test_terrain_analytics_e2e.cpp)
target_link_libraries(test_terrain_analytics_e2e PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  sicnu_processing
  sicnu_operators
  GDAL::GDAL
)
sicnu_link_jsoncpp(test_terrain_analytics_e2e)
target_include_directories(test_terrain_analytics_e2e PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/tests
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_terrain_analytics_e2e)

# Terrain agent tools 11.0: spatial:terrain_profile / viewshed inspect
# JSON contracts, Unicode paths, error codes.
add_executable(test_terrain_agent_tools test_terrain_agent_tools.cpp)
target_link_libraries(test_terrain_agent_tools PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  sicnu_processing
  sicnu_agent
  GDAL::GDAL
)
sicnu_link_jsoncpp(test_terrain_agent_tools)
target_include_directories(test_terrain_agent_tools PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/tests
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_terrain_agent_tools)

# Scientific Algorithms 9.0 (M0): regression tests for the verified
# scientific defects #848/#853/#854/#855/#856/#873 (old-code-fails form).
add_executable(test_scientific_defects_9 test_scientific_defects_9.cpp)
target_link_libraries(test_scientific_defects_9 PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  sicnu_processing
  sicnu_operators
  GDAL::GDAL
)
sicnu_link_jsoncpp(test_scientific_defects_9)
target_include_directories(test_scientific_defects_9 PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_scientific_defects_9)

# Scientific Algorithms 9.0 (M1): mechanical drift guards binding the numeric
# / raster semantics contracts to anchors in the owned sources.
add_executable(test_semantic_drift_9 test_semantic_drift_9.cpp)
target_link_libraries(test_semantic_drift_9 PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  sicnu_processing
  sicnu_operators
  GDAL::GDAL
)
sicnu_link_jsoncpp(test_semantic_drift_9)
target_include_directories(test_semantic_drift_9 PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
target_compile_definitions(test_semantic_drift_9 PRIVATE
  CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
sicnu_discover_tests(test_semantic_drift_9)

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

# Raster merge-bands algorithm tests (mixed input types, #1035)
add_executable(test_raster_merge_bands test_raster_merge_bands.cpp)
target_link_libraries(test_raster_merge_bands PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
  GDAL::GDAL
)
target_include_directories(test_raster_merge_bands PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_raster_merge_bands)

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
  Sicnu::experiment
  Sicnu::experiment_bridge
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

# Surface-11 discovery parity gate: union projection vs MCP tools/list vs
# dispatch tables. Compiles mcp_server.cpp in-process (same pattern as
# test_mcp_server) so both sides share one registry state.
add_executable(test_surface_parity test_surface_parity.cpp ../src/agent/mcp_server.cpp ../src/app/shell/processing_job_adapter.cpp)
target_link_libraries(test_surface_parity PRIVATE
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
  Sicnu::experiment
  Sicnu::experiment_bridge
  GDAL::GDAL
)
if(TARGET sicnu_operators)
  target_link_libraries(test_surface_parity PRIVATE sicnu_operators)
endif()
if(TARGET jsoncpp)
  target_link_libraries(test_surface_parity PRIVATE jsoncpp)
endif()
target_include_directories(test_surface_parity PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
target_compile_definitions(test_surface_parity PRIVATE SICNU_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_compile_definitions(test_surface_parity PRIVATE
  SICNU_TEST_SURFACE_CLI="$<TARGET_FILE:sicnu_geo_rs_cli>"
)
sicnu_discover_tests(test_surface_parity)

# Track D1 (ds41-capability-help-sync): capability surface parity gate.
# registry descriptors <-> sidecar A <-> sidecar B <-> help operator topics
# <-> MCP list_algorithms/get_algorithm_schema <-> CLI algorithms list/--schema.
# Compiles mcp_server.cpp in-process (same pattern as test_surface_parity) so
# the MCP handlers share one registry state with the assertions.
add_executable(test_capability_surface_parity test_capability_surface_parity.cpp ../src/agent/mcp_server.cpp ../src/app/shell/processing_job_adapter.cpp)
target_link_libraries(test_capability_surface_parity PRIVATE
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
  Sicnu::experiment
  Sicnu::experiment_bridge
  GDAL::GDAL
)
if(TARGET sicnu_operators)
  target_link_libraries(test_capability_surface_parity PRIVATE sicnu_operators)
endif()
if(TARGET jsoncpp)
  target_link_libraries(test_capability_surface_parity PRIVATE jsoncpp)
endif()
target_include_directories(test_capability_surface_parity PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
target_compile_definitions(test_capability_surface_parity PRIVATE SICNU_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_compile_definitions(test_capability_surface_parity PRIVATE
  SICNU_TEST_CAPABILITY_CLI="$<TARGET_FILE:sicnu_geo_rs_cli>"
)
add_dependencies(test_capability_surface_parity sicnu_geo_rs_cli)
sicnu_discover_tests(test_capability_surface_parity)

# Track D1: capability completeness gate (summary / failure_modes / io
# contracts, with the enumerated collection-style io.inputs exemptions).
sicnu_add_test(test_capability_completeness)
sicnu_link_jsoncpp(test_capability_completeness)

# Track I (ds41-capability-search-13): authoritative algorithm-search
# contract — golden corpus over injected + live descriptor universes,
# mutation potency, bounds/typed refusal, deterministic ordering, and
# CLI<->MCP cross-surface parity (mcp_server.cpp compiled in-process so the
# tools/call path shares one registry with the engine assertions; the CLI
# runs as a real subprocess).
add_executable(test_algorithm_search test_algorithm_search.cpp ../src/agent/mcp_server.cpp ../src/app/shell/processing_job_adapter.cpp)
target_link_libraries(test_algorithm_search PRIVATE
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
  Sicnu::experiment
  Sicnu::experiment_bridge
  GDAL::GDAL
)
if(TARGET sicnu_operators)
  target_link_libraries(test_algorithm_search PRIVATE sicnu_operators)
endif()
sicnu_link_jsoncpp(test_algorithm_search)
target_include_directories(test_algorithm_search PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
target_compile_definitions(test_algorithm_search PRIVATE
  SICNU_TEST_SEARCH_CLI="$<TARGET_FILE:sicnu_geo_rs_cli>"
)
add_dependencies(test_algorithm_search sicnu_geo_rs_cli)
sicnu_discover_tests(test_algorithm_search)

# Surface-11 protocol suite: progress relay, artifact_read, redaction,
# initialize negatives. Same in-process mcp_server.cpp pattern.
add_executable(test_surface_protocol test_surface_protocol.cpp ../src/agent/mcp_server.cpp ../src/app/shell/processing_job_adapter.cpp)
target_link_libraries(test_surface_protocol PRIVATE
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
  Sicnu::experiment
  Sicnu::experiment_bridge
  GDAL::GDAL
)
if(TARGET sicnu_operators)
  target_link_libraries(test_surface_protocol PRIVATE sicnu_operators)
endif()
if(TARGET jsoncpp)
  target_link_libraries(test_surface_protocol PRIVATE jsoncpp)
endif()
target_include_directories(test_surface_protocol PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
target_compile_definitions(test_surface_protocol PRIVATE SICNU_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
sicnu_discover_tests(test_surface_protocol)

# Surface-11 WP-H: real stdio E2E. The host binary speaks the wire protocol
# through the same mcp_server.cpp the desktop binary hosts; the test spawns
# it and drives the full agent loop twice.
add_executable(surface_mcp_host surface_mcp_host_main.cpp ../src/agent/mcp_server.cpp ../src/app/shell/processing_job_adapter.cpp)
target_link_libraries(surface_mcp_host PRIVATE
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
  sicnu_task_center
  sicnu_data
  sicnu_agent
  Sicnu::experiment
  Sicnu::experiment_bridge
  GDAL::GDAL
)
if(TARGET sicnu_operators)
  target_link_libraries(surface_mcp_host PRIVATE sicnu_operators)
endif()
if(TARGET jsoncpp)
  target_link_libraries(surface_mcp_host PRIVATE jsoncpp)
endif()
target_include_directories(surface_mcp_host PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)

add_executable(test_surface_e2e test_surface_e2e.cpp)
target_link_libraries(test_surface_e2e PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
)
target_compile_definitions(test_surface_e2e PRIVATE
  SICNU_TEST_SURFACE_HOST="$<TARGET_FILE:surface_mcp_host>"
  SICNU_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
target_include_directories(test_surface_e2e PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_surface_e2e)

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

# ExecutionId value type: frozen wire-form contract ("task-<id>") and the
# engine-jobId rejection semantics.
sicnu_add_test(test_execution_id)

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

    # Platform 12.0 model ensemble: manifest contract parsing/validation,
    # member resolution, weighted_mean / weighted_vote combination against
    # hand-computed answers, uncertainty bands, atomic failure and the
    # surface exclusions.
    add_executable(test_model_ensemble test_model_ensemble.cpp)
    target_link_libraries(test_model_ensemble PRIVATE
        Catch2::Catch2WithMain
        Qt6::Core
        qgis_core
        sicnu_processing
        sicnu_task_center
        sicnu_operators
        ${OpenCV_LIBS}
    )
    sicnu_link_jsoncpp(test_model_ensemble)
    target_include_directories(test_model_ensemble PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/tests
        ${CMAKE_BINARY_DIR}
    )
    sicnu_discover_tests(test_model_ensemble)

    # Platform 12.0 provider strategy: ordered fallback chain over the SAME
    # artifact — registry verdicts, typed all-fail errors and the payload /
    # provenance reporting that makes a fired fallback observable.
    add_executable(test_provider_fallback test_provider_fallback.cpp)
    target_link_libraries(test_provider_fallback PRIVATE
        Catch2::Catch2WithMain
        Qt6::Core
        qgis_core
        sicnu_processing
        sicnu_task_center
        sicnu_operators
        ${OpenCV_LIBS}
    )
    sicnu_link_jsoncpp(test_provider_fallback)
    target_include_directories(test_provider_fallback PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/tests
        ${CMAKE_BINARY_DIR}
    )
    sicnu_discover_tests(test_provider_fallback)

    # Platform 12.0 session-pool concurrency stress: device pinning,
    # eviction-while-held lifetime, ledger conservation under races.
    add_executable(test_model_runtime_stress test_model_runtime_stress.cpp)
    target_link_libraries(test_model_runtime_stress PRIVATE
        Catch2::Catch2WithMain
        Qt6::Core
        qgis_core
        sicnu_processing
        sicnu_task_center
        sicnu_operators
        ${OpenCV_LIBS}
    )
    sicnu_link_jsoncpp(test_model_runtime_stress)
    target_include_directories(test_model_runtime_stress PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/tests
        ${CMAKE_BINARY_DIR}
    )
    sicnu_discover_tests(test_model_runtime_stress)

    # Platform 12.0 tile geometry Oracle: byte-identical probability stacks
    # across batch/tile/overlap splits, extreme-integer refusals and the
    # terminal-OOM atomicity (no output, no sidecar, no residue).
    add_executable(test_tile_semantic_equivalence test_tile_semantic_equivalence.cpp)
    target_link_libraries(test_tile_semantic_equivalence PRIVATE
        Catch2::Catch2WithMain
        Qt6::Core
        qgis_core
        sicnu_processing
        sicnu_task_center
        sicnu_operators
        ${OpenCV_LIBS}
    )
    sicnu_link_jsoncpp(test_tile_semantic_equivalence)
    target_include_directories(test_tile_semantic_equivalence PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/tests
        ${CMAKE_BINARY_DIR}
    )
    sicnu_discover_tests(test_tile_semantic_equivalence)

    # Model Runtime 8.0: CRS-aware grid authority + provenance, temporal
    # sequence lane (dynamic T, timestamps, quality masks), placement policy
    # and the Labels class remap. Fake providers + synthetic rasters — runs
    # in every OpenCV-enabled build.
    add_executable(test_model_runtime_8 test_model_runtime_8.cpp)
    target_link_libraries(test_model_runtime_8 PRIVATE
        Catch2::Catch2WithMain
        Qt6::Core
        qgis_core
        sicnu_processing
        sicnu_task_center
        sicnu_operators
        ${OpenCV_LIBS}
    )
    sicnu_link_jsoncpp(test_model_runtime_8)
    target_include_directories(test_model_runtime_8 PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/tests
        ${CMAKE_BINARY_DIR}
    )
    sicnu_discover_tests(test_model_runtime_8)

    # Model Runtime 9.0: contract truth (schema ⊇ parsed params, #872 class),
    # real device inventory, per-feed preprocessing, temporal chunking,
    # feather blending, packaging identity, consumer-side provenance.
    # Fake providers + synthetic rasters — runs in every OpenCV-enabled build.
    add_executable(test_model_runtime_9 test_model_runtime_9.cpp)
    target_link_libraries(test_model_runtime_9 PRIVATE
        Catch2::Catch2WithMain
        Qt6::Core
        qgis_core
        sicnu_processing
        sicnu_task_center
        sicnu_operators
        ${OpenCV_LIBS}
    )
    sicnu_link_jsoncpp(test_model_runtime_9)
    target_include_directories(test_model_runtime_9 PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/tests
        ${CMAKE_BINARY_DIR}
    )
    sicnu_discover_tests(test_model_runtime_9)

    # Model Runtime 8.0 WP-A: the REAL ONNX Runtime lane. Built only when the
    # ORT provider is actually compiled in — hosts without the dependency skip
    # the target entirely (honest capability gate, never a fake pass).
    if(SICNU_WITH_ONNX_RUNTIME AND ONNXRUNTIME_INCLUDE_DIR AND ONNXRUNTIME_LIBRARY)
        add_executable(test_onnxruntime_provider test_onnxruntime_provider.cpp)
        target_include_directories(test_onnxruntime_provider PRIVATE ${ONNXRUNTIME_INCLUDE_DIR})
        # The CUDA-EP presence probe calls the ORT C++ API directly.
        target_link_libraries(test_onnxruntime_provider PRIVATE ${ONNXRUNTIME_LIBRARY})
        target_link_libraries(test_onnxruntime_provider PRIVATE
            Catch2::Catch2WithMain
            Qt6::Core
            qgis_core
            sicnu_processing
            sicnu_task_center
            sicnu_operators
            ${OpenCV_LIBS}
        )
        sicnu_link_jsoncpp(test_onnxruntime_provider)
        target_include_directories(test_onnxruntime_provider PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/tests
            ${CMAKE_BINARY_DIR}
        )
        sicnu_discover_tests(test_onnxruntime_provider)
    endif()

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

    # Platform 10.0: F-OPS-5 regression — grid-bounded, cancellable detection NMS.
    add_executable(test_detection_nms_10 test_detection_nms_10.cpp)
    target_link_libraries(test_detection_nms_10 PRIVATE
        Catch2::Catch2WithMain
        Qt6::Core
        qgis_core
        sicnu_processing
        sicnu_task_center
        sicnu_operators
        ${OpenCV_LIBS}
    )
    sicnu_link_jsoncpp(test_detection_nms_10)
    target_include_directories(test_detection_nms_10 PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/tests
        ${CMAKE_BINARY_DIR}
    )
    sicnu_discover_tests(test_detection_nms_10)

    # Platform 10.0: EO model platform regressions (F-OPS fixes + manifest/tasks).
    add_executable(test_eo_platform_10 test_eo_platform_10.cpp)
    target_link_libraries(test_eo_platform_10 PRIVATE
        Catch2::Catch2WithMain
        Qt6::Core
        qgis_core
        sicnu_processing
        sicnu_task_center
        sicnu_operators
        ${OpenCV_LIBS}
    )
    sicnu_link_jsoncpp(test_eo_platform_10)
    target_include_directories(test_eo_platform_10 PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/tests
        ${CMAKE_BINARY_DIR}
    )
    sicnu_discover_tests(test_eo_platform_10)
endif()

# Platform 4.0: model runtime benchmark (env-gated: SICNU_MODEL_BENCH=1)
if(SICNU_HAS_OPENCV AND OpenCV_FOUND)
    add_executable(test_model_runtime_bench test_model_runtime_bench.cpp)
    # The env-gated benchmark gains an ORT-lane section when the provider is
    # compiled in (the same macro the provider body uses).
    if(SICNU_WITH_ONNX_RUNTIME AND ONNXRUNTIME_INCLUDE_DIR AND ONNXRUNTIME_LIBRARY)
        target_compile_definitions(test_model_runtime_bench PRIVATE SICNU_WITH_ONNX_RUNTIME=1)
        target_include_directories(test_model_runtime_bench PRIVATE ${ONNXRUNTIME_INCLUDE_DIR})
        # OrtGetVersionString() for the bench header.
        target_link_libraries(test_model_runtime_bench PRIVATE ${ONNXRUNTIME_LIBRARY})
    endif()
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

# LSEE 10.0 external-memory contracts (scratch leases, disk tile store, tile
# checkpoint; Qt-free; links sicnu_runtime only).
sicnu_add_test(test_external_memory_10 TIMEOUT 120)
target_link_libraries(test_external_memory_10 PRIVATE sicnu_runtime)

# LSEE 10.0 scale & failure matrix (fan-out joins at 10^6 logical tiles under
# SICNU_LSEE10_STRESS, scratch storms, checkpoint restart, poison-task
# convergence). Drives the real TaskCenter/JobEngine singletons: serial.
sicnu_add_test(test_large_scale_execution_10 TIMEOUT 900 RUN_SERIAL TRUE)

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

# MLOps 9.0 split-boundary validation: #875 regression, total config
# validation (NaN/Inf/negative/overflow), degenerate-fold refusals,
# spatiotemporal_block, bounded generation summaries.
sicnu_add_test(test_mlops9_split TIMEOUT 120)
target_link_libraries(test_mlops9_split PRIVATE Sicnu::dataset SQLite::SQLite3)

# MLOps 9.0 dataset version DAG: parent validation, ancestor walks with
# corruption detection, derive/fork; sample temporal-validity governance.
sicnu_add_test(test_mlops9_version_dag TIMEOUT 120)
target_link_libraries(test_mlops9_version_dag PRIVATE Sicnu::dataset SQLite::SQLite3)

# MLOps 9.0 evidence/matrix/replay/promotion over a real ExperimentStore.
sicnu_add_test(test_mlops9_evidence TIMEOUT 300)
target_link_libraries(test_mlops9_evidence PRIVATE Sicnu::experiment Sicnu::dataset SQLite::SQLite3)

# RS14-06 Experiment Debugger (ADR 0174): normalized run snapshots, step
# alignment, first-divergence classification over recorded evidence. The
# debugger lib carries its own deps (Sicnu::experiment/dataset/workflow);
# SQLite is listed explicitly to match the experiment-suite convention.
sicnu_add_test(test_experiment_debugger TIMEOUT 300)
target_link_libraries(test_experiment_debugger PRIVATE
  Sicnu::experiment_debugger Sicnu::experiment Sicnu::dataset SQLite::SQLite3)

# MLOps 9.0 scale & durability: 100k-run metadata stress, concurrent
# readers/writer, corruption refusal, injected commit-fault recovery.
# RUN_SERIAL: pays 100k single-row transactions by design.
# RUN_SERIAL TRUE is forwarded through sicnu_discover_tests as a real ctest
# test property (a target-level RUN_SERIAL would be a no-op for discovered
# tests — review round 1).
sicnu_add_test(test_mlops9_scale TIMEOUT 900 RUN_SERIAL TRUE)
target_link_libraries(test_mlops9_scale PRIVATE
  Sicnu::experiment Sicnu::dataset SQLite::SQLite3 sicnu_runtime)

# MLOps 9.0 CLI auto-recording E2E: real sicnu_geo_rs_cli subprocess with
# --experiment-record; POSIX spawn (popen) like test_cli_commands_json.
if(NOT WIN32)
add_executable(test_mlops9_cli_record test_mlops9_cli_record.cpp)
target_link_libraries(test_mlops9_cli_record PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Sicnu::experiment
  Sicnu::dataset
  GDAL::GDAL
  SQLite::SQLite3
)
target_include_directories(test_mlops9_cli_record PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
target_compile_definitions(test_mlops9_cli_record PRIVATE
  SICNU_TEST_CLI="$<TARGET_FILE:sicnu_geo_rs_cli>"
)
sicnu_discover_tests(test_mlops9_cli_record TIMEOUT 300)
endif()


# D19 Dataset Foundry: role/catalog/feature join/QA report/foundry service.
sicnu_add_test(test_d19_dataset_foundry TIMEOUT 300)
target_link_libraries(test_d19_dataset_foundry PRIVATE Sicnu::dataset SQLite::SQLite3)

# D19 Scientific Benchmark Definition + headless runner + compare/service.
sicnu_add_test(test_d19_benchmark TIMEOUT 180)
target_link_libraries(test_d19_benchmark PRIVATE Sicnu::experiment Sicnu::dataset SQLite::SQLite3)

sicnu_add_test(test_d19_foundry_benchmark_chain TIMEOUT 180)
target_link_libraries(test_d19_foundry_benchmark_chain PRIVATE Sicnu::experiment Sicnu::dataset SQLite::SQLite3)

# Foundation 5.0 experiment/run identity, metrics, comparison, lineage and
# reproduction bundle (ADR 0137/0138, goal §23-§31/§51).
sicnu_add_test(test_experiment_evaluation TIMEOUT 180)
target_link_libraries(test_experiment_evaluation PRIVATE Sicnu::experiment Sicnu::dataset SQLite::SQLite3)

# RS14-17 reproducibility capsule: LIGHT executable on purpose — links only
# the capsule projection module + experiment/dataset (Qt/Core level). The
# default sicnu_add_test chain would drag QGIS/agent/operators into a
# pure-document contract suite.
qt_add_executable(test_experiment_capsule test_experiment_capsule.cpp)
target_link_libraries(test_experiment_capsule PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Sicnu::experiment_capsule
)
target_include_directories(test_experiment_capsule PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
target_compile_definitions(test_experiment_capsule PRIVATE
  CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
sicnu_discover_tests(test_experiment_capsule TIMEOUT 120)

# Foundation 5.0 dataset composition/label QA + the 100k metadata scale
# contract (goal §20/§37/§46/§52). RUN_SERIAL: the 100k benchmark should not
# compete with other tests for memory.
sicnu_add_test(test_dataset_quality_scale TIMEOUT 600)
target_link_libraries(test_dataset_quality_scale PRIVATE Sicnu::dataset SQLite::SQLite3)
set_property(TARGET test_dataset_quality_scale PROPERTY RUN_SERIAL TRUE)

# 12.0 data-foundation scale Oracle (O1): 100k-sample catalog + 10k-run
# experiment store — batch ingest, keyset-cursor paging, indexed identity
# lookup, with a structured JSON perf report emitted to
# SICNU_PERF_EVIDENCE_DIR. RUN_SERIAL: the benchmark should not compete for
# memory.
sicnu_add_test(test_data_foundation_scale TIMEOUT 900 RUN_SERIAL TRUE
  ENVIRONMENT "SICNU_PERF_EVIDENCE_DIR=${CMAKE_BINARY_DIR}/test-artifacts")
target_link_libraries(test_data_foundation_scale PRIVATE Sicnu::dataset Sicnu::experiment SQLite::SQLite3)
set_property(TARGET test_data_foundation_scale PROPERTY RUN_SERIAL TRUE)

# 12.0 data-foundation fault Oracle (O3): deterministic mid-transaction
# failures, corrupt rows and batch-commit faults leave no partial rows or
# phantom references.
sicnu_add_test(test_store_fault_injection TIMEOUT 300)
target_link_libraries(test_store_fault_injection PRIVATE Sicnu::dataset Sicnu::experiment SQLite::SQLite3 sicnu_runtime)

# 12.0 GC/retention Oracle (O4): governance graph fixture proves dry-run ==
# execution and reachability; experiment run prune plan/execute equivalence.
sicnu_add_test(test_gc_retention_plan TIMEOUT 300)
target_link_libraries(test_gc_retention_plan PRIVATE Sicnu::data Sicnu::dataset Sicnu::experiment SQLite::SQLite3)

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

# Platform 8.0 MLOps bridge core: execution→experiment lifecycle recording
# (truthful states, pins, stale reconciliation). Light target; the workflow
# adapter E2E lives in test_mlops8_e2e.
sicnu_add_test(test_mlops8_bridge TIMEOUT 300)
target_link_libraries(test_mlops8_bridge PRIVATE Sicnu::experiment Sicnu::dataset SQLite::SQLite3)

# Platform 8.0 scale probe: reconcile/scan boundedness at 20k stored runs
# (RUN_SERIAL: it pays 20k single-row transactions by design).
sicnu_add_test(test_mlops8_scale TIMEOUT 900)
target_link_libraries(test_mlops8_scale PRIVATE Sicnu::experiment Sicnu::dataset SQLite::SQLite3)
set_property(TARGET test_mlops8_scale PROPERTY RUN_SERIAL TRUE)

# Platform 8.0 MLOps E2E: real tracked pipelines auto-recorded into the
# ExperimentStore through the workflow→experiment adapter.
add_executable(test_mlops8_e2e test_mlops8_e2e.cpp)
target_link_libraries(test_mlops8_e2e PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Sicnu::experiment
  Sicnu::experiment_bridge
  SQLite::SQLite3
  sicnu_workflow
  sicnu_workflow_runtime
  sicnu_data
  sicnu_processing
  sicnu_task_center
  sicnu_jobs
)
if(TARGET jsoncpp)
  target_link_libraries(test_mlops8_e2e PRIVATE jsoncpp)
endif()
target_include_directories(test_mlops8_e2e PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_mlops8_e2e TIMEOUT 600)

# D5 Lab Report & Lineage: sicnu.labreport.v1 schema/writers plus the desktop
# lab auto-recorder end-to-end (real tracked pipelines → ExperimentStore →
# report). Same link set as test_mlops8_e2e (bridge needs workflow + jobs).
add_executable(test_lab_report test_lab_report.cpp)
target_link_libraries(test_lab_report PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Sicnu::experiment
  Sicnu::experiment_bridge
  SQLite::SQLite3
  sicnu_workflow
  sicnu_workflow_runtime
  sicnu_data
  sicnu_processing
  sicnu_task_center
  sicnu_jobs
  sicnu_operators
)
if(TARGET jsoncpp)
  target_link_libraries(test_lab_report PRIVATE jsoncpp)
endif()
target_include_directories(test_lab_report PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_lab_report TIMEOUT 600)


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

# RemoteDatasetPool first-use initialization concurrency (issue #1047) —
# driven through a local GeoTIFF, so unlike test_remote_source_cache it needs
# no POSIX sockets and runs on every platform. Lean link set on purpose: the
# pool is a Qt Core + GDAL data-layer primitive; pulling the GUI stack in
# would only slow the targeted lane. Discovered under Catch2 case names (no
# TEST_PREFIX), so select with `ctest -R "RemoteDatasetPool"`.
qt_add_executable(test_remote_pool_init test_remote_pool_init.cpp)
target_link_libraries(test_remote_pool_init PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  sicnu_data
  GDAL::GDAL
)
target_include_directories(test_remote_pool_init PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_remote_pool_init TIMEOUT 180)

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

# Execution Plane 8.0: admission scaling (indexed ready heap + bucketed
# engine queue), worker containment, retry evidence, resume identity, cache
# identity. RUN_SERIAL: stress cases share the TaskCenter/JobEngine
# singletons and measure wall-clock scaling.
sicnu_add_test(test_execution_plane_8 TIMEOUT 1200 RUN_SERIAL TRUE)
target_compile_definitions(test_execution_plane_8 PRIVATE
    SICNU_WORKER_EXE="$<TARGET_FILE:sicnu_worker>"
)

# Execution / Concurrency / Lifecycle 9.0 — structured hierarchy, transient
# child admission (#862), lifecycle-safe eventing (#860), ghost-run closure
# (#876). RUN_SERIAL: heavy concurrency cases must not share CPU/RSS with
# other heavy suites (no cross-case singleton sharing — each Catch2 case is
# its own process via catch_discover_tests).
sicnu_add_test(test_execution_plane_9 TIMEOUT 2400 RUN_SERIAL TRUE)

# Track 12 TaskCenter runtime — aging fairness, bounded pending queue,
# cancel watchdog, weight admission, execution telemetry. RUN_SERIAL:
# shares the TaskCenter/JobEngine singletons and asserts wall-clock
# ordering (aged promotion) that must not race neighboring suites.
sicnu_add_test(test_task_center_12 TIMEOUT 300 RUN_SERIAL TRUE)

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
  ${CMAKE_SOURCE_DIR}/src/app/widgets/band_composite_palette.cpp
  ${CMAKE_SOURCE_DIR}/src/processing/gdal/gdal_dataset_wrapper.cpp
)
target_link_libraries(test_spectral_profile_widget PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
  Qt6::Concurrent
  qgis_core
  qgis_gui
  sicnu_processing
  GDAL::GDAL
)
target_include_directories(test_spectral_profile_widget PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
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

# Classification & Object Intelligence 11.0 — OpenCV-free contract tests
# (class order, probability calibration, uncertainty, feature schema).
add_executable(test_class_order test_class_order.cpp)
target_link_libraries(test_class_order PRIVATE
  Catch2::Catch2WithMain
  qgis_core
  qgis_analysis
  Qt6::Core
)
target_include_directories(test_class_order PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/classification
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_class_order)

add_executable(test_probability_calibration test_probability_calibration.cpp)
target_link_libraries(test_probability_calibration PRIVATE
  Catch2::Catch2WithMain
  qgis_core
  qgis_analysis
  Qt6::Core
)
target_include_directories(test_probability_calibration PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/classification
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_probability_calibration)

add_executable(test_uncertainty test_uncertainty.cpp)
target_link_libraries(test_uncertainty PRIVATE
  Catch2::Catch2WithMain
  qgis_core
  qgis_analysis
  Qt6::Core
)
target_include_directories(test_uncertainty PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/classification
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_uncertainty)

add_executable(test_feature_schema test_feature_schema.cpp)
target_link_libraries(test_feature_schema PRIVATE
  Catch2::Catch2WithMain
  qgis_core
  qgis_analysis
  Qt6::Core
)
target_include_directories(test_feature_schema PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/classification
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_feature_schema)

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

  # SAR Radiometric State 13.0 (Track E): operator census, declared-state
  # behavioral gates, derived-product semantics, DN LUT calibration and the
  # end-to-end chain provenance oracle.
  sicnu_add_test(test_sar_radiometric_state)

  # Platform 3.0 feature cube contract.
  sicnu_add_test(test_feature_cube)

  # Platform 3.0 temporal fit numeric references (goal §7/§12).
  sicnu_add_test(test_temporal_fit)

  # Temporal Phenology 12.0 (WP2): day-axis smoothing kernels and gap-fill
  # provenance on irregular time axes.
  sicnu_add_test(test_temporal_irregular)

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

  # Temporal Platform 10.0: regular-calendar normalization, joint
  # seasonal-trend change, multi-region extraction, region features.
  add_executable(test_temporal_calendar test_temporal_calendar.cpp)
  target_link_libraries(test_temporal_calendar PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    qgis_core
    Sicnu::data
    sicnu_processing
    GDAL::GDAL
  )
  target_include_directories(test_temporal_calendar PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/processing/algorithms/temporal
    ${CMAKE_BINARY_DIR}
  )
  sicnu_link_jsoncpp(test_temporal_calendar)
  sicnu_discover_tests(test_temporal_calendar)

  add_executable(test_temporal_change test_temporal_change.cpp)
  target_link_libraries(test_temporal_change PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    qgis_core
    Sicnu::data
    sicnu_processing
    GDAL::GDAL
  )
  target_include_directories(test_temporal_change PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/processing/algorithms/temporal
    ${CMAKE_BINARY_DIR}
  )
  sicnu_link_jsoncpp(test_temporal_change)
  sicnu_discover_tests(test_temporal_change)

  # Temporal Intelligence 11.0: attribution/model-selection, uncertainty and
  # phenology-2.0 kernels over the shared synthetic corpus helper.
  add_executable(test_temporal_selection test_temporal_selection.cpp)
  target_link_libraries(test_temporal_selection PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    qgis_core
    Sicnu::data
    sicnu_processing
    GDAL::GDAL
  )
  target_include_directories(test_temporal_selection PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/processing/algorithms/temporal
    ${CMAKE_SOURCE_DIR}/tests
    ${CMAKE_BINARY_DIR}
  )
  sicnu_link_jsoncpp(test_temporal_selection)
  sicnu_discover_tests(test_temporal_selection)

  add_executable(test_temporal_uncertainty test_temporal_uncertainty.cpp)
  target_link_libraries(test_temporal_uncertainty PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    qgis_core
    Sicnu::data
    sicnu_processing
    GDAL::GDAL
  )
  target_include_directories(test_temporal_uncertainty PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/processing/algorithms/temporal
    ${CMAKE_SOURCE_DIR}/tests
    ${CMAKE_BINARY_DIR}
  )
  sicnu_link_jsoncpp(test_temporal_uncertainty)
  sicnu_discover_tests(test_temporal_uncertainty)

  add_executable(test_temporal_phenology_multi test_temporal_phenology_multi.cpp)
  target_link_libraries(test_temporal_phenology_multi PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    qgis_core
    Sicnu::data
    sicnu_processing
    GDAL::GDAL
  )
  target_include_directories(test_temporal_phenology_multi PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/processing/algorithms/temporal
    ${CMAKE_SOURCE_DIR}/tests
    ${CMAKE_BINARY_DIR}
  )
  sicnu_link_jsoncpp(test_temporal_phenology_multi)
  sicnu_discover_tests(test_temporal_phenology_multi)

  add_executable(test_temporal_operators_ti11 test_temporal_operators_ti11.cpp)
  target_link_libraries(test_temporal_operators_ti11 PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    qgis_core
    Sicnu::data
    sicnu_processing
    sicnu_operators
    GDAL::GDAL
  )
  target_include_directories(test_temporal_operators_ti11 PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/processing/algorithms/temporal
    ${CMAKE_BINARY_DIR}
  )
  sicnu_link_jsoncpp(test_temporal_operators_ti11)
  sicnu_discover_tests(test_temporal_operators_ti11)

  add_executable(test_temporal_regions test_temporal_regions.cpp)
  target_link_libraries(test_temporal_regions PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    qgis_core
    Sicnu::data
    sicnu_processing
    GDAL::GDAL
  )
  target_include_directories(test_temporal_regions PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/processing/algorithms/temporal
    ${CMAKE_BINARY_DIR}
  )
  sicnu_link_jsoncpp(test_temporal_regions)
  sicnu_discover_tests(test_temporal_regions)

  add_executable(test_temporal_operators_10 test_temporal_operators_10.cpp)
  target_link_libraries(test_temporal_operators_10 PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    qgis_core
    Sicnu::data
    sicnu_processing
    sicnu_operators
    GDAL::GDAL
  )
  target_include_directories(test_temporal_operators_10 PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/processing/algorithms/temporal
    ${CMAKE_BINARY_DIR}
  )
  sicnu_link_jsoncpp(test_temporal_operators_10)
  sicnu_discover_tests(test_temporal_operators_10)

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

  # Spectral Intelligence 11.0: dual-window local RX (kernel known answers).
  add_executable(test_spectral_local_rx test_spectral_local_rx.cpp)
  target_link_libraries(test_spectral_local_rx PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    qgis_core
    sicnu_processing
  )
  target_include_directories(test_spectral_local_rx PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
  )
  sicnu_link_jsoncpp(test_spectral_local_rx)
  sicnu_discover_tests(test_spectral_local_rx)

  # Spectral Intelligence 11.0: sparse (L1/non-negative) unmixing closed forms.
  add_executable(test_spectral_sparse_unmixing test_spectral_sparse_unmixing.cpp)
  target_link_libraries(test_spectral_sparse_unmixing PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    qgis_core
    sicnu_processing
  )
  target_include_directories(test_spectral_sparse_unmixing PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
  )
  sicnu_link_jsoncpp(test_spectral_sparse_unmixing)
  sicnu_discover_tests(test_spectral_sparse_unmixing)

  # Spectral Intelligence 11.0: SID-SAM hybrid similarity closed forms.
  add_executable(test_spectral_hybrid_similarity test_spectral_hybrid_similarity.cpp)
  target_link_libraries(test_spectral_hybrid_similarity PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    qgis_core
    sicnu_processing
  )
  target_include_directories(test_spectral_hybrid_similarity PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
  )
  sicnu_link_jsoncpp(test_spectral_hybrid_similarity)
  sicnu_discover_tests(test_spectral_hybrid_similarity)

  # Spectral Intelligence 11.0: endmember analysis (reduction, angle matrix,
  # sensor projection on the master resampling seam).
  add_executable(test_endmember_analysis test_endmember_analysis.cpp)
  target_link_libraries(test_endmember_analysis PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    qgis_core
    sicnu_processing
  )
  target_include_directories(test_endmember_analysis PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
  )
  sicnu_link_jsoncpp(test_endmember_analysis)
  sicnu_discover_tests(test_endmember_analysis)

  # Spectral Intelligence 11.0: 256-1024 band scale evidence (closed forms,
  # determinism, memory-bound refusals; wall-clock is never a gate).
  add_executable(test_spectral_scale test_spectral_scale.cpp)
  target_link_libraries(test_spectral_scale PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    sicnu_processing
  )
  target_include_directories(test_spectral_scale PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
  )
  sicnu_discover_tests(test_spectral_scale)

  # Spectral Intelligence 11.0: end-to-end artifact chain over the registry
  # (PPI table -> endmember analysis -> sparse unmixing; local RX; hybrid
  # similarity) with provenance custody assertions at every hop.
  add_executable(test_spectral_pipeline_11 test_spectral_pipeline_11.cpp)
  target_link_libraries(test_spectral_pipeline_11 PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    Qt6::Gui
    qgis_core
    sicnu_processing
    sicnu_operators
    GDAL::GDAL
  )
  target_include_directories(test_spectral_pipeline_11 PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
  )
  sicnu_link_jsoncpp(test_spectral_pipeline_11)
  sicnu_discover_tests(test_spectral_pipeline_11)

  # Spectral Workbench 11 panel: offscreen widget test (compiles the widget
  # TU directly, mirroring the test_spectral_profile_widget pattern).
  add_executable(test_spectral_workbench_panel test_spectral_workbench_panel.cpp
    ${CMAKE_SOURCE_DIR}/src/app/widgets/spectral_workbench_panel.cpp
  )
  target_link_libraries(test_spectral_workbench_panel PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    qgis_core
    sicnu_processing
  )
  target_include_directories(test_spectral_workbench_panel PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
  )
  sicnu_discover_tests(test_spectral_workbench_panel)

  # Spectral library domain tests - JSON round-trip, malformed input.
  add_executable(test_spectral_library test_spectral_library.cpp)
  target_link_libraries(test_spectral_library PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    sicnu_core
    sicnu_processing
  )
  target_include_directories(test_spectral_library PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
  )
  sicnu_discover_tests(test_spectral_library)

  # Built-in spectral library data contract (D12): strict validation of the
  # shipped data/spectral/ deliverable, provenance/LICENSES drift guards,
  # material-prior queries, sensor resampling and SAM/SID smoke matching.
  add_executable(test_spectral_library_data test_spectral_library_data.cpp)
  target_link_libraries(test_spectral_library_data PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    sicnu_processing
  )
  target_include_directories(test_spectral_library_data PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
  )
  target_compile_definitions(test_spectral_library_data PRIVATE
    CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
  )
  sicnu_discover_tests(test_spectral_library_data)

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

# Histogram Widget tests — data structures + widget-level scan behavior
# (Workbench 9.0 M0: failed GDALOpen must surface an error on the GUI thread)
add_executable(test_histogram_widget
  test_histogram_widget.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/histogram_widget.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/rs_scan_pool.cpp
)
target_link_libraries(test_histogram_widget PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
  qgis_core
  qgis_gui
  GDAL::GDAL
)
target_include_directories(test_histogram_widget PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_histogram_widget PROPERTIES AUTOMOC ON)
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

# Guided Workflow Widget tests — the LabSpec data layer AND the widget's own
# session-boundary state machine. The widget-level tests construct the real
# widget offscreen, so the target also compiles the widget + GuiJobHandle and
# links sicnu_task_center (GuiJobHandle's default TaskCenter) plus the QGIS
# headers pulled in by the main_window.h include chain.
add_executable(test_guided_workflow_widget test_guided_workflow_widget.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/guided_workflow_widget.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/lab_spec_loader.cpp
  ${CMAKE_SOURCE_DIR}/src/app/shell/gui_job_adapter.cpp
)
target_link_libraries(test_guided_workflow_widget PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_task_center
  jsoncpp
)
target_include_directories(test_guided_workflow_widget PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
target_compile_definitions(test_guided_workflow_widget PRIVATE
  CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
set_target_properties(test_guided_workflow_widget PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_guided_workflow_widget)

# LabSpec drift guards: shipped labs vs the Processing Registry (operator ids,
# param schemas, grading refs, generated docs). Full processing stack needed.
sicnu_add_test(test_labspec)
target_sources(test_labspec PRIVATE
  ${CMAKE_SOURCE_DIR}/src/app/widgets/lab_spec_loader.cpp
)

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
# Band selection / library selection operators (Hyperspectral Platform
# 10.0 work packages F/G): wavelength-window picks with unit normalization,
# bad-band exclusion, material filters and near-duplicate QA.
add_executable(test_spectral_selection test_spectral_selection.cpp)
target_link_libraries(test_spectral_selection PRIVATE
  Catch2::Catch2WithMain
sicnu_operators
sicnu_processing
  Qt6::Core
  qgis_core
  GDAL::GDAL
)
if(TARGET jsoncpp)
target_link_libraries(test_spectral_selection PRIVATE jsoncpp)
endif()
target_include_directories(test_spectral_selection PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_BINARY_DIR})
sicnu_discover_tests(test_spectral_selection)

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

  # Spectral pipeline (Hyperspectral Platform 10.0): PPI -> unmixing/SAM in
  # one workflow through placeholder-bound spectral-table artifacts, plus the
  # shared reference seam's typed refusals.
  add_executable(test_spectral_pipeline test_spectral_pipeline.cpp)
  target_link_libraries(test_spectral_pipeline PRIVATE
    Catch2::Catch2WithMain
    sicnu_workflow
    sicnu_workflow_runtime
    sicnu_operators
    sicnu_processing
    sicnu_task_center
    sicnu_jobs
    Sicnu::data
    Qt6::Core
    qgis_core
    GDAL::GDAL
  )
  if(TARGET jsoncpp)
    target_link_libraries(test_spectral_pipeline PRIVATE jsoncpp)
  endif()
  target_include_directories(test_spectral_pipeline PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_BINARY_DIR})
  sicnu_discover_tests(test_spectral_pipeline)

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
  ${CMAKE_SOURCE_DIR}/src/app/shell/workbench_enum_provider.cpp
  ${CMAKE_SOURCE_DIR}/src/app/shell/schema_form_builder.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/rs_scan_pool.cpp
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
  ${CMAKE_SOURCE_DIR}/src/app/panels/asset_catalog_index.cpp
  ${CMAKE_SOURCE_DIR}/src/app/preview/asset_preview_service.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/rs_scan_pool.cpp
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
  Qt6::Concurrent
  qgis_core
  qgis_gui
  sicnu_processing
  sicnu_task_center
  sicnu_data
  sicnu_jobs
  sicnu_operators
  Sicnu::qgis_display
  Sicnu::Geospatial
  GDAL::GDAL
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

# ── Workbench 9.0 M0: project clear/switch/exit churn stress (#859 family) ──
add_executable(test_project_lifecycle_stress
  test_project_lifecycle_stress.cpp
)
target_link_libraries(test_project_lifecycle_stress PRIVATE
  Catch2::Catch2
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  Sicnu::qgis_display
)
target_include_directories(test_project_lifecycle_stress PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
# Heavy churn loop — run serially, never parallel with other heavy suites.
sicnu_discover_tests(test_project_lifecycle_stress)

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

# Chinese satellite product adapters (GF-1/2/6, ZY-3, HJ-1A/1B CCD; ADR 0146) —
# identity/refusal, CRESDA sidecar parsing, band-role tables, import operators,
# and the headless GF-1 end-to-end chain (import → NDVI → supervised
# classification). Synthetic fixtures only; no real imagery.
add_executable(test_cn_products test_cn_products.cpp)
target_link_libraries(test_cn_products PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Test
  qgis_core
  sicnu_geospatial
  sicnu_processing
  sicnu_operators
  GDAL::GDAL
)
target_include_directories(test_cn_products PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_link_jsoncpp(test_cn_products)
sicnu_discover_tests(test_cn_products)

# CN sensor registry schema 2.0 (ADR 0159) — strict field validation,
# band_axis contracts, registry-wide validator and the committed-data drift
# gate. Qt-free; fixtures are handwritten minimal registry JSON.
sicnu_add_io_test(test_sensor_schema)

# ADR 0159 CN product families (GF-3 SAR, GF-4 PMI, GF-5 AHSI, ZY-1
# 02B/02D/02E, CBERS-4 INPE generation) — identity, sidecar generation
# separation, declared-metadata parsing, hyperspectral band-axis aggregation,
# refined refusals. Synthetic fixtures only.
add_executable(test_cn_product_families11 test_cn_product_families11.cpp)
target_link_libraries(test_cn_product_families11 PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Test
  qgis_core
  sicnu_geospatial
  GDAL::GDAL
)
target_include_directories(test_cn_product_families11 PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
target_compile_definitions(test_cn_product_families11 PRIVATE
  CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
sicnu_link_jsoncpp(test_cn_product_families11)
sicnu_discover_tests(test_cn_product_families11)

# ADR 0159 import-plan hardening — dry-run constituent graph with checksums,
# cooperative cancellation (zero-half-product), read-only sources, Chinese
# paths, and the GF-3 SAR family through plan→execute. Synthetic fixtures.
add_executable(test_product_import_plan11 test_product_import_plan11.cpp)
target_link_libraries(test_product_import_plan11 PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Test
  qgis_core
  sicnu_geospatial
  sicnu_processing
  sicnu_operators
  GDAL::GDAL
)
target_include_directories(test_product_import_plan11 PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_link_jsoncpp(test_product_import_plan11)
sicnu_discover_tests(test_product_import_plan11)

# ADR 0159 CN product fixture corpus — sidecar XML fixtures + golden
# metadata + manifest drift gate. TIFFs are synthesized at runtime.
add_executable(test_cn_product_fixtures test_cn_product_fixtures.cpp)
target_link_libraries(test_cn_product_fixtures PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Test
  qgis_core
  sicnu_geospatial
  GDAL::GDAL
)
target_include_directories(test_cn_product_fixtures PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
target_compile_definitions(test_cn_product_fixtures PRIVATE
  CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
sicnu_link_jsoncpp(test_cn_product_fixtures)
sicnu_discover_tests(test_cn_product_fixtures)

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
  sicnu_operators
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
  ${CMAKE_SOURCE_DIR}/src/app/widgets/rs_scan_pool.cpp
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

# SchemaForm 4.0 (Professional Workbench 8.0): nested objects, object arrays,
# dynamic enum sources, async checks, a11y.
add_executable(test_schema_form_4
  test_schema_form_4.cpp
  ${CMAKE_SOURCE_DIR}/src/app/shell/schema_form_builder.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/rs_scan_pool.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/crs_selector.cpp
)
target_link_libraries(test_schema_form_4 PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Concurrent
  qgis_core
  qgis_gui
  sicnu_processing
  sicnu_operators
  sicnu_data
  GDAL::GDAL
  Sicnu::help
)
target_include_directories(test_schema_form_4 PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
sicnu_link_jsoncpp(test_schema_form_4)
set_target_properties(test_schema_form_4 PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_schema_form_4)

# AssetPreviewService (Professional Workbench 8.0, package E): bounded async
# raster/vector previews with cache + cancellation semantics.
add_executable(test_asset_preview_service
  test_asset_preview_service.cpp
  ${CMAKE_SOURCE_DIR}/src/app/preview/asset_preview_service.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/rs_scan_pool.cpp
)
target_link_libraries(test_asset_preview_service PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Concurrent
  qgis_core
  qgis_gui
  Sicnu::Geospatial
  GDAL::GDAL
)
target_include_directories(test_asset_preview_service PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_asset_preview_service PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_asset_preview_service)

# Context facts + suggested next action (Professional Workbench 8.0, package C)
add_executable(test_context_facts_8
  test_context_facts_8.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/selection_context.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/workbench_host.cpp
)
target_link_libraries(test_context_facts_8 PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
  qgis_core
  qgis_gui
)
target_include_directories(test_context_facts_8 PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_context_facts_8 PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_context_facts_8)

# Asset catalog index + panel scaling contract (Professional Workbench 8.0,
# package D): filter, bounded truthful rendering, lazy collection children.
add_executable(test_asset_catalog_index
  test_asset_catalog_index.cpp
  ${CMAKE_SOURCE_DIR}/src/app/panels/asset_catalog_index.cpp
  ${CMAKE_SOURCE_DIR}/src/app/panels/data_manager_panel.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/rs_empty_state_widget.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/dialog_help_catalog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/dialog_utils.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/temporal_analysis_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/raster_processing_dialog_base.cpp
  ${CMAKE_SOURCE_DIR}/src/app/shell/gui_job_adapter.cpp
  ${CMAKE_SOURCE_DIR}/src/app/shell/processing_job_adapter.cpp
  ${CMAKE_SOURCE_DIR}/src/app/preview/asset_preview_service.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/rs_scan_pool.cpp
)
target_link_libraries(test_asset_catalog_index PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
  Qt6::Concurrent
  qgis_core
  qgis_gui
  sicnu_processing
  sicnu_task_center
  sicnu_data
  sicnu_jobs
  sicnu_operators
  sicnu_workflow
  Sicnu::qgis_display
  Sicnu::Geospatial
  GDAL::GDAL
)
target_include_directories(test_asset_catalog_index PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
sicnu_link_jsoncpp(test_asset_catalog_index)
set_target_properties(test_asset_catalog_index PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_asset_catalog_index)

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

# Behavior-level shortcut host guardrail (UI review C1): real key events
# against the hidden-menubar vs window-hosted action contract.
add_executable(test_shortcut_hosting
  test_shortcut_hosting.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/command_registry.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/selection_context.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/workbench_host.cpp
)
target_link_libraries(test_shortcut_hosting PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
  qgis_core
  qgis_gui
)
target_include_directories(test_shortcut_hosting PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
target_compile_definitions(test_shortcut_hosting PRIVATE
  CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
sicnu_discover_tests(test_shortcut_hosting)

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

# ── Workbench 9.0 M8: plugin command lifecycle (placement → registry) ──────
add_executable(test_plugin_ui_placement
  test_plugin_ui_placement.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/plugin_command_defs.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/command_registry.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/selection_context.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/workbench_host.cpp
)
target_link_libraries(test_plugin_ui_placement PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
)
target_include_directories(test_plugin_ui_placement PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_plugin_ui_placement PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_plugin_ui_placement)

# ── Workbench 9.0 M7: bounded catalog pagination ───────────────────────────
add_executable(test_catalog_pagination
  test_catalog_pagination.cpp
  ${CMAKE_SOURCE_DIR}/src/app/panels/data_manager_panel.cpp
  ${CMAKE_SOURCE_DIR}/src/app/panels/asset_catalog_index.cpp
  ${CMAKE_SOURCE_DIR}/src/app/preview/asset_preview_service.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/rs_scan_pool.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/rs_empty_state_widget.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/dialog_help_catalog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/dialog_utils.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/temporal_analysis_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/raster_processing_dialog_base.cpp
  ${CMAKE_SOURCE_DIR}/src/app/shell/gui_job_adapter.cpp
  ${CMAKE_SOURCE_DIR}/src/app/shell/processing_job_adapter.cpp
)
target_link_libraries(test_catalog_pagination PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
  Qt6::Concurrent
  qgis_core
  qgis_gui
  sicnu_processing
  sicnu_task_center
  sicnu_data
  sicnu_jobs
  sicnu_operators
  Sicnu::qgis_display
  Sicnu::Geospatial
  GDAL::GDAL
)
target_include_directories(test_catalog_pagination PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_catalog_pagination PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_catalog_pagination)

# ── Workbench 9.0 M6: production schema enum provider ──────────────────────
add_executable(test_workbench_enum_provider
  test_workbench_enum_provider.cpp
  ${CMAKE_SOURCE_DIR}/src/app/shell/workbench_enum_provider.cpp
)
target_link_libraries(test_workbench_enum_provider PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  sicnu_data
  sicnu_operators
  GDAL::GDAL
)
target_include_directories(test_workbench_enum_provider PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_workbench_enum_provider PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_workbench_enum_provider)

# ── Workbench 9.0 M1: explicit workbench state model ───────────────────────
add_executable(test_workbench_state_model
  test_workbench_state_model.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/workbench_state.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/selection_context.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/workbench_host.cpp
)
target_link_libraries(test_workbench_state_model PRIVATE
  Catch2::Catch2
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
  qgis_core
  qgis_gui
)
target_include_directories(test_workbench_state_model PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_workbench_state_model PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_workbench_state_model)

# ── Workbench 9.0 M0: worker→UI completion delivery helper ─────────────────
add_executable(test_marshal_ui
  test_marshal_ui.cpp
)
target_link_libraries(test_marshal_ui PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
)
target_include_directories(test_marshal_ui PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_marshal_ui PROPERTIES AUTOMOC ON)
# AUTOUIC would treat the marshal header as a Qt Designer ui_*.h output.
set_property(SOURCE test_marshal_ui.cpp PROPERTY SKIP_AUTOUIC ON)
sicnu_discover_tests(test_marshal_ui)

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
  ${CMAKE_SOURCE_DIR}/src/app/shell/workbench_enum_provider.cpp
  ${CMAKE_SOURCE_DIR}/src/app/shell/schema_form_builder.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/rs_scan_pool.cpp
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
if(TARGET sicnu_geo_rs_cli)
  add_dependencies(test_workflow_cache_e2e sicnu_geo_rs_cli)
endif()
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
  ${CMAKE_SOURCE_DIR}/src/app/panels/asset_catalog_index.cpp
  ${CMAKE_SOURCE_DIR}/src/app/preview/asset_preview_service.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/rs_scan_pool.cpp
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
  SICNU_TEST_PLUGIN_HOST_WORKER="$<TARGET_FILE:exprs_plugin_host_worker>"
)
# #1186: target-scoped builds must build runtime-loaded fixtures first.
add_dependencies(test_plugin_host_process isolation_plugin exprs_plugin_host_worker)

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
add_dependencies(test_exprs_plugin_loader hello_plugin)

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
# Plugin-platform 8.0: declarative out-of-process UI schema contract.
sicnu_add_sdk_test(test_plugin_ui_schema)
# Public SDK install surface: installed-header include closure + the
# four-site plugin API version parity. Reads the source tree only.
sicnu_add_sdk_test(test_sdk_public_surface)
# CLI 3.0 public command surface: accepted ↔ dispatched ↔ documented parity.
# Reads src/cli/cli_commands.cpp + docs/headless/README.md (source tree only).
sicnu_add_sdk_test(test_cli_command_surface)

sicnu_add_test(test_exprs_workflow_schema)
target_link_libraries(test_exprs_workflow_schema PRIVATE sicnu_sdk)

# popen()/WEXITSTATUS CLI-spawn fixture is POSIX-only.
if(NOT WIN32)
# Surface-11: batch manifest runner (unit level, no CLI binary needed).
sicnu_add_test(test_cli_batch_manifest)
target_link_libraries(test_cli_batch_manifest PRIVATE sicnu_cli_batch sicnu_sdk)

sicnu_add_test(test_cli_commands_json)
target_link_libraries(test_cli_commands_json PRIVATE sicnu_sdk sicnu_plugins_framework)
# 8.0: data identity / data cache check tests drive the CLI against an
# in-test loopback range server (shared fixture with the io suites).
target_sources(test_cli_commands_json PRIVATE support/http_range_server.cpp)
target_include_directories(test_cli_commands_json PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(WIN32)
  target_link_libraries(test_cli_commands_json PRIVATE ws2_32)
endif()
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

# Plugin-platform 8.0: declarative UI schema renderer (host-owned widgets,
# fake delegate; offscreen Qt).
sicnu_add_test(test_plugin_ui_schema_host)
target_link_libraries(test_plugin_ui_schema_host PRIVATE sicnu_plugins_framework sicnu_sdk)


# — Harness 4.0 (Pi Spatial Scientist & Agent Harness) —
# Error taxonomy (Phase 12) and canonical tool manifest/taxonomy (Phases 1/2).
sicnu_add_test(test_harness_error)
sicnu_link_jsoncpp(test_harness_error)


# — Scientific Workflow Compiler 10.0 (ADR 0149) —
# Typed WorkflowIR: fail-closed reader, closed fact vocabulary, bounds,
# deterministic normalize/fingerprint, structural validation.
sicnu_add_test(test_workflow_ir)
sicnu_link_jsoncpp(test_workflow_ir)

# Static analysis: typed issues over typed facts, fact-status severity
# discipline, deterministic order.
sicnu_add_test(test_workflow_analysis)
sicnu_link_jsoncpp(test_workflow_analysis)

# Deterministic repair insertion: closed rule table, risk classes, evidence
# records, decision refusals, idempotence.
sicnu_add_test(test_workflow_repair)
sicnu_link_jsoncpp(test_workflow_repair)

# Fact model 2.0 (compiler & grounding 11): temporal cadence/regularity,
# unit-aware resolution, quality masks, product/model/resource facts.
sicnu_add_test(test_workflow_facts_11)
sicnu_link_jsoncpp(test_workflow_facts_11)

# Analysis 2.0 (compiler & grounding 11): temporal calendar, numeric-domain
# chain, band identity, output identity.
sicnu_add_test(test_workflow_analysis_11)
sicnu_link_jsoncpp(test_workflow_analysis_11)

# Bounded grounding probes (compiler & grounding 11).
sicnu_add_test(test_grounding_probes_11)
sicnu_link_jsoncpp(test_grounding_probes_11)

# Provenance projection + prepared decisions + explain (compiler & grounding 11).
sicnu_add_test(test_provenance_projection_11)
sicnu_link_jsoncpp(test_provenance_projection_11)

# Harness session checkpoint/resume with stale invalidation + budgeted
# deterministic tool shortlist.
sicnu_add_test(test_context_checkpoint)
sicnu_link_jsoncpp(test_context_checkpoint)
sicnu_add_test(test_tool_shortlist)
sicnu_link_jsoncpp(test_tool_shortlist)

# The staged planner: compileWorkflow over IR/recipe, plan<->IR conversion,
# lowering with provenance.
sicnu_add_test(test_workflow_planner)
sicnu_link_jsoncpp(test_workflow_planner)

sicnu_add_test(test_harness_catalog)
sicnu_link_jsoncpp(test_harness_catalog)


sicnu_add_test(test_harness_grounding)
sicnu_link_jsoncpp(test_harness_grounding)


# Harness 4.0 evals: six deterministic RS scenarios + anti-hallucination +
# token budgets (mission Phases 18-20).
sicnu_add_test(test_harness_evals)
sicnu_add_test(test_harness_evidence)
sicnu_add_test(test_harness_eval_corpus)
sicnu_link_jsoncpp(test_harness_evals)

# D9: lab copilot (teaching mode) evaluation suite — must-refuse reverse
# cases, diagnostic brain, step anchoring, budgets.
sicnu_add_test(test_harness_lab_evals)
sicnu_link_jsoncpp(test_harness_lab_evals)


# Harness 7.0 capability-knowledge drift guard: entries vs the live operator
# registry, operator x-rs-contract facts, and the closed intent vocabulary.
sicnu_add_test(test_capability_drift)
sicnu_link_jsoncpp(test_capability_drift)


# Contract Platform 9.0: mechanical implementation↔schema projection guard
# (the permanent fix class for #872/#879/#880) + canonical descriptor tests.
sicnu_add_test(test_contract_projection_9)
target_link_libraries(test_contract_projection_9 PRIVATE sicnu_contracts)
sicnu_link_jsoncpp(test_contract_projection_9)

# Contract Platform 9.0: command/help/action reference graph (#869/#881/#882)
# and the HelpContentStore single-load regression (#871).
sicnu_add_test(test_command_contract_9)
target_link_libraries(test_command_contract_9 PRIVATE sicnu_contracts)
sicnu_link_jsoncpp(test_command_contract_9)

# Contract Platform 9.0: error-taxonomy ↔ diagnostics census (#870).
sicnu_add_test(test_diagnostics_contract_9)
target_link_libraries(test_diagnostics_contract_9 PRIVATE sicnu_contracts)
sicnu_link_jsoncpp(test_diagnostics_contract_9)

# Contract Platform 9.0: capability knowledge / tool / recipe floors (M4).
sicnu_add_test(test_capability_contract_9)
target_link_libraries(test_capability_contract_9 PRIVATE sicnu_contracts)
sicnu_link_jsoncpp(test_capability_contract_9)

# Contract Platform 9.0: contract inventory graph + snapshot freshness (M0).
sicnu_add_test(test_contract_platform_9)
target_link_libraries(test_contract_platform_9 PRIVATE sicnu_contracts)
sicnu_link_jsoncpp(test_contract_platform_9)

# RS14-11: evidence-first agent session core (pure C++; no Qt/GDAL link).
add_executable(test_agent_loop_core test_agent_loop_core.cpp)
target_link_libraries(test_agent_loop_core PRIVATE
  Catch2::Catch2WithMain
  sicnu_agent_loop
)
sicnu_discover_tests(test_agent_loop_core)

# RS14-11: seam interfaces, deterministic offline doubles, session policy.
add_executable(test_agent_loop_seams test_agent_loop_seams.cpp)
target_link_libraries(test_agent_loop_seams PRIVATE
  Catch2::Catch2WithMain
  sicnu_agent_loop
)
sicnu_discover_tests(test_agent_loop_seams)

# RS14-11: session driver — dry-run / plan-only / execute-with-verify modes.
add_executable(test_agent_loop_modes test_agent_loop_modes.cpp)
target_link_libraries(test_agent_loop_modes PRIVATE
  Catch2::Catch2WithMain
  sicnu_agent_loop
)
sicnu_discover_tests(test_agent_loop_modes)

# RS14-11: full-loop E2E on deterministic offline doubles (slices D/E/F).
add_executable(test_agent_loop_e2e test_agent_loop_e2e.cpp)
target_link_libraries(test_agent_loop_e2e PRIVATE
  Catch2::Catch2WithMain
  sicnu_agent_loop
)
sicnu_discover_tests(test_agent_loop_e2e)

# RS14-11: journal restart/resume of a cancelled session.
add_executable(test_agent_loop_resume test_agent_loop_resume.cpp)
target_link_libraries(test_agent_loop_resume PRIVATE
  Catch2::Catch2WithMain
  sicnu_agent_loop
)
sicnu_discover_tests(test_agent_loop_resume)

# Agent Operations & Recovery Control Center (Qt-free core).
add_executable(test_agent_ops_core test_agent_ops_core.cpp)
target_link_libraries(test_agent_ops_core PRIVATE
  Catch2::Catch2WithMain
  sicnu_agent_ops
)
sicnu_discover_tests(test_agent_ops_core)

# Agent Ops Control Center UI offscreen smoke (optional when UI lib present).
if(TARGET sicnu_agent_ops_ui)
  add_executable(test_agent_ops_ui_smoke test_agent_ops_ui_smoke.cpp)
  target_link_libraries(test_agent_ops_ui_smoke PRIVATE
    Catch2::Catch2WithMain
    sicnu_agent_ops_ui
    Qt6::Widgets
  )
  sicnu_discover_tests(test_agent_ops_ui_smoke)
endif()

# Unified Scientific Verifier Slice A (#1191): status lattice, versioned
# spec/report serde, error codes, sha256 (pure C++; no Qt/GDAL link).
add_executable(test_verifier_schema test_verifier_schema.cpp)
target_link_libraries(test_verifier_schema PRIVATE
  Catch2::Catch2WithMain
  sicnu_verifier
)
sicnu_discover_tests(test_verifier_schema TEST_PREFIX "test_verifier_schema::")

# Unified Scientific Verifier engine completion (#1191 slices B..G): check
# dispatch, packs, two-level rollup, renders, projections and the
# adversarial battery. All light targets: sicnu_verifier + Catch2 + jsoncpp
# only — no Qt, no GDAL, no QGIS link.
add_executable(test_verifier_engine test_verifier_engine.cpp)
target_link_libraries(test_verifier_engine PRIVATE
  Catch2::Catch2WithMain
  sicnu_verifier
)
sicnu_discover_tests(test_verifier_engine TEST_PREFIX "test_verifier_engine::")

add_executable(test_verifier_numeric test_verifier_numeric.cpp)
target_link_libraries(test_verifier_numeric PRIVATE
  Catch2::Catch2WithMain
  sicnu_verifier
)
sicnu_discover_tests(test_verifier_numeric TEST_PREFIX "test_verifier_numeric::")

add_executable(test_verifier_provenance test_verifier_provenance.cpp)
target_link_libraries(test_verifier_provenance PRIVATE
  Catch2::Catch2WithMain
  sicnu_verifier
)
sicnu_discover_tests(test_verifier_provenance TEST_PREFIX "test_verifier_provenance::")

add_executable(test_verifier_packs test_verifier_packs.cpp)
target_link_libraries(test_verifier_packs PRIVATE
  Catch2::Catch2WithMain
  sicnu_verifier
)
sicnu_discover_tests(test_verifier_packs TEST_PREFIX "test_verifier_packs::")

add_executable(test_verifier_levels test_verifier_levels.cpp)
target_link_libraries(test_verifier_levels PRIVATE
  Catch2::Catch2WithMain
  sicnu_verifier
)
sicnu_discover_tests(test_verifier_levels TEST_PREFIX "test_verifier_levels::")

add_executable(test_verifier_render test_verifier_render.cpp)
target_link_libraries(test_verifier_render PRIVATE
  Catch2::Catch2WithMain
  sicnu_verifier
)
sicnu_discover_tests(test_verifier_render TEST_PREFIX "test_verifier_render::")

add_executable(test_verifier_adversarial test_verifier_adversarial.cpp)
target_link_libraries(test_verifier_adversarial PRIVATE
  Catch2::Catch2WithMain
  sicnu_verifier
)
sicnu_discover_tests(test_verifier_adversarial TEST_PREFIX "test_verifier_adversarial::")

# RS14-02 Scientific Preflight Engine: typed findings + versioned, deterministic
# PreflightReport schema (pure C++; no Qt/GDAL link).
add_executable(test_preflight_report_schema test_preflight_report_schema.cpp)
target_link_libraries(test_preflight_report_schema PRIVATE
  Catch2::Catch2WithMain
  sicnu_preflight
)
sicnu_discover_tests(test_preflight_report_schema)

# RS14-03 Scientific Repair Planner: versioned repair_plan/1.0 schema,
# SHA-256/16 fingerprints, fail-closed envelope reader (Qt-free leaf).
add_executable(test_repair_planner_schema test_repair_planner_schema.cpp)
target_link_libraries(test_repair_planner_schema PRIVATE
  Catch2::Catch2WithMain
  sicnu_repair_planner
)
sicnu_discover_tests(test_repair_planner_schema)

# RS14-03 completion: requirement synthesis, provider-driven candidates,
# auto policy, deterministic plan/result/fragment, teaching/agent views and
# the tamper-evident planning state (Qt-free leaf; the integration case reads
# the real capability knowledge documents).
add_executable(test_repair_planner_completion test_repair_planner_completion.cpp)
target_link_libraries(test_repair_planner_completion PRIVATE
  Catch2::Catch2WithMain
  sicnu_repair_planner
)
target_compile_definitions(test_repair_planner_completion PRIVATE
  CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
sicnu_discover_tests(test_repair_planner_completion)

# RS14-20 Scientific Recipe Compiler (#1206): schema, compiler, validator,
# registry, lookup, equivalence and the lab document/source contract —
# Qt-free leaves over sicnu_recipes.
add_executable(test_recipe_schema test_recipe_schema.cpp)
target_link_libraries(test_recipe_schema PRIVATE
  Catch2::Catch2WithMain
  sicnu_recipes
)
sicnu_discover_tests(test_recipe_schema)

add_executable(test_recipe_compiler test_recipe_compiler.cpp)
target_link_libraries(test_recipe_compiler PRIVATE
  Catch2::Catch2WithMain
  sicnu_recipes
)
sicnu_discover_tests(test_recipe_compiler)

add_executable(test_recipe_validator test_recipe_validator.cpp)
target_link_libraries(test_recipe_validator PRIVATE
  Catch2::Catch2WithMain
  sicnu_recipes
)
sicnu_discover_tests(test_recipe_validator)

add_executable(test_recipe_registry test_recipe_registry.cpp)
target_link_libraries(test_recipe_registry PRIVATE
  Catch2::Catch2WithMain
  sicnu_recipes
)
sicnu_discover_tests(test_recipe_registry)

add_executable(test_recipe_lookup test_recipe_lookup.cpp)
target_link_libraries(test_recipe_lookup PRIVATE
  Catch2::Catch2WithMain
  sicnu_recipes
)
sicnu_discover_tests(test_recipe_lookup)

add_executable(test_recipe_equivalence test_recipe_equivalence.cpp)
target_link_libraries(test_recipe_equivalence PRIVATE
  Catch2::Catch2WithMain
  sicnu_recipes
)
# The equivalence gate reads the shipped labs, sidecar catalogs and committed
# artifacts straight from the source tree.
target_compile_definitions(test_recipe_equivalence PRIVATE
  CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
sicnu_discover_tests(test_recipe_equivalence)

add_executable(test_lab_document test_lab_document.cpp)
target_link_libraries(test_lab_document PRIVATE
  Catch2::Catch2WithMain
  sicnu_recipes
)
sicnu_discover_tests(test_lab_document)

add_executable(test_lab_source test_lab_source.cpp)
target_link_libraries(test_lab_source PRIVATE
  Catch2::Catch2WithMain
  sicnu_recipes
)
sicnu_discover_tests(test_lab_source)


# Structure oracle: build/test wiring drift gate — every src/**/CMakeLists
# reachable from root, every src/**/*.cpp named by an eligible build script,
# every test_*.cpp registered. Pure std::filesystem; no Qt/GDAL link.
add_executable(test_build_wiring_drift test_build_wiring_drift.cpp)
target_link_libraries(test_build_wiring_drift PRIVATE
  Catch2::Catch2WithMain
)
target_include_directories(test_build_wiring_drift PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(test_build_wiring_drift PRIVATE
  CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
set_target_properties(test_build_wiring_drift PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
sicnu_discover_tests(test_build_wiring_drift)

# RS14-18 Undergraduate Curriculum Pack: curriculum catalog/progress/availability
# over the agent harness (sicnu_agent already in the sicnu_add_test stack).
sicnu_add_test(test_curriculum)
sicnu_link_jsoncpp(test_curriculum)
# The shipped-manifest case anchors the real repo data; without this define
# it falls back to "." and only passes when run from the source root.
target_compile_definitions(test_curriculum PRIVATE
  SICNU_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

# RS14-15 Explainable Scientific Workflow: provenance + step explanation schema
# (sicnu_explain links jsoncpp PUBLIC).
add_executable(test_explain_schema test_explain_schema.cpp)
target_link_libraries(test_explain_schema PRIVATE
  Catch2::Catch2WithMain
  sicnu_explain
)
sicnu_discover_tests(test_explain_schema)

# RS14-13 Scientific Fault Injection Lab: registry, expectations, fixtures,
# observables, diagnosis (sicnu_faultlab links jsoncpp PUBLIC).
add_executable(test_faultlab test_faultlab.cpp)
target_link_libraries(test_faultlab PRIVATE
  Catch2::Catch2WithMain
  sicnu_faultlab
)
sicnu_discover_tests(test_faultlab)

# RS14-05 Process-aware Experiment Grader: engine/types/error/json/sha256
# schema (sicnu_grader links jsoncpp PUBLIC).
add_executable(test_grader_schema test_grader_schema.cpp)
target_link_libraries(test_grader_schema PRIVATE
  Catch2::Catch2WithMain
  sicnu_grader
)
sicnu_discover_tests(test_grader_schema)

# RS14-05 Slices B/C: matching/scoring engine, document-shape adapters and
# report renderers (same leaf lane, no new links).
add_executable(test_grader_engine test_grader_engine.cpp)
target_link_libraries(test_grader_engine PRIVATE
  Catch2::Catch2WithMain
  sicnu_grader
)
sicnu_discover_tests(test_grader_engine)

add_executable(test_grader_adapters test_grader_adapters.cpp)
target_link_libraries(test_grader_adapters PRIVATE
  Catch2::Catch2WithMain
  sicnu_grader
)
sicnu_discover_tests(test_grader_adapters)

add_executable(test_grader_render test_grader_render.cpp)
target_link_libraries(test_grader_render PRIVATE
  Catch2::Catch2WithMain
  sicnu_grader
)
sicnu_discover_tests(test_grader_render)

# LabSpec v3 runtime: checkpoint verify, hint policy, session state/store,
# spec runtime (sicnu_lab_runtime links jsoncpp PUBLIC).
add_executable(test_lab_runtime test_lab_runtime.cpp)
target_link_libraries(test_lab_runtime PRIVATE
  Catch2::Catch2WithMain
  sicnu_lab_runtime
)
sicnu_discover_tests(test_lab_runtime)

# Contract Platform 10.0: scientific contract registry completeness and
# known answers against the LIVE operator registry (append-only block).
sicnu_add_test(test_scientific_contract_10)
target_link_libraries(test_scientific_contract_10 PRIVATE sicnu_contracts)
sicnu_link_jsoncpp(test_scientific_contract_10)

# Contract Platform 10.0: cross-projection drift gates for the projections
# contract-9 did not bind (determinism agreement, sidecar io projection,
# LabSpec operator references).
sicnu_add_test(test_drift_projection_10)
target_link_libraries(test_drift_projection_10 PRIVATE sicnu_contracts)
sicnu_link_jsoncpp(test_drift_projection_10)

# Verification Platform 10.0: metamorphic invariants, reproducibility
# replay, seed determinism, bounded CRS refusal fuzz, provenance checks.
sicnu_add_test(test_science_verification_10)
target_link_libraries(test_science_verification_10 PRIVATE sicnu_contracts)
sicnu_link_jsoncpp(test_science_verification_10)

# Verification Platform 11.0 (F09 census 2.0): determinism & contract
# coverage census bound to the live registry; scanner honesty via a
# synthetic registration tree; committed census snapshot byte gate
# (append-only block).
sicnu_add_test(test_contract_census_11)
target_link_libraries(test_contract_census_11 PRIVATE sicnu_contracts)
sicnu_link_jsoncpp(test_contract_census_11)

# Contract Platform 9.0: contract generation cost baseline (M8). Evidence
# artifact via `benchmark_contract9 --out <json>`.
# Temporal Platform 10.0 scale evidence (regularize / harmonic-breaks /
# 100k-region reducer / per-year phenology). Evidence only, never a gate:
# benchmark_temporal10 --out benchmarks/temporal10.json
add_executable(benchmark_temporal10
  benchmark_temporal10.cpp
)
target_link_libraries(benchmark_temporal10 PRIVATE
  sicnu_processing
  Qt6::Core
)
if(TARGET jsoncpp)
  target_link_libraries(benchmark_temporal10 PRIVATE jsoncpp)
endif()
target_include_directories(benchmark_temporal10 PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}
)

add_executable(benchmark_contract9
  benchmark_contract9.cpp
)
target_link_libraries(benchmark_contract9 PRIVATE
  sicnu_contracts
  sicnu_operators
  Qt6::Core
)
if(TARGET jsoncpp)
  target_link_libraries(benchmark_contract9 PRIVATE jsoncpp)
endif()
target_include_directories(benchmark_contract9 PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_definitions(benchmark_contract9 PRIVATE
  CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
# Harness 9.0 contract floors: suggested-action resolution (#881) and recipe
# gate truth/degradation semantics (#867), cross-checked against the live
# tool registry and the authoritative workbench command definitions.
sicnu_add_test(test_harness9_contracts)
sicnu_link_jsoncpp(test_harness9_contracts)


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

# ── RS14-12 teaching autonomy ladder ───────────────────────────────────────
# Policy-layer tests: Catch2 + Sicnu::autonomy (jsoncpp transitively) only —
# no Qt, no QGIS — so the level semantics, schema, decision engine, audit
# log and projection suites build and run fast. Gate-wiring tests that must
# exercise the real seams use the standard heavy helper (sicnu_add_test).
function(sicnu_add_autonomy_test NAME)
  add_executable(${NAME} ${NAME}.cpp)
  target_link_libraries(${NAME} PRIVATE
    Catch2::Catch2WithMain
    Sicnu::autonomy
  )
  target_include_directories(${NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
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

sicnu_add_autonomy_test(test_autonomy_policy)
sicnu_add_autonomy_test(test_autonomy_classification)
sicnu_add_autonomy_test(test_autonomy_decision)
sicnu_add_autonomy_test(test_autonomy_precedence)
sicnu_add_autonomy_test(test_autonomy_audit)
sicnu_add_autonomy_test(test_autonomy_projection)
sicnu_add_autonomy_test(test_autonomy_holder)

# Gate wiring: exercises the real seams (lab copilot entry point, the
# SpatialToolRegistry execute path for harness:execute_plan), so it uses the
# standard heavy helper.
sicnu_add_test(test_autonomy_gate)
sicnu_link_jsoncpp(test_autonomy_gate)

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
target_link_libraries(test_help_coverage PRIVATE Sicnu::help Sicnu::help_adapters Sicnu::help_gui sicnu_contracts)

# F20 UX guidance corpus: scenario table over the live help registry +
# diagnostics catalog + contextual resolver. Same shape as test_help_coverage
# (help_gui pulls the app-side adapter; the shell trio is compiled in).
sicnu_add_test(test_ux_guidance_corpus)
target_sources(test_ux_guidance_corpus PRIVATE
  ${CMAKE_SOURCE_DIR}/src/app/workbench/selection_context.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/command_registry.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/workbench_host.cpp
  ${CMAKE_SOURCE_DIR}/data/help/help_content.qrc
)
target_include_directories(test_ux_guidance_corpus PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
)
target_compile_definitions(test_ux_guidance_corpus PRIVATE
    CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
target_link_libraries(test_ux_guidance_corpus PRIVATE Sicnu::help Sicnu::help_adapters Sicnu::help_gui)

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

# =============================================================================
# Verification Platform 8.0 — appended at end of file on purpose (same
# convention as the 7.0 block above: this file is shared across tracks).
# =============================================================================

# Cross-platform/GDAL compile contract (task A): gdal_compat.h capability
# coherence + behavioral pin of the version-selected VSI code. Light lane
# (Catch2 + Sicnu::Geospatial only).
sicnu_add_io_test(test_portability_contract)

# Header self-containment probes (task A, failure class F1 / PR #833): every
# nominated first-party leaf header must compile as the FIRST include of a
# cold translation unit. OBJECT libraries = compile-only (nothing to link,
# no runtime test); the L0 lane of scripts/verification_ladder.py builds the
# aggregate target so a missing-include regression fails before any heavy
# target compiles.
function(sicnu_add_header_probes)
  set(_probe_libs "")
  foreach(header IN LISTS ARGN)
    string(REGEX REPLACE "[/\\.-]" "_" probe "header_probe_${header}")
    set(_probe_src "${CMAKE_CURRENT_BINARY_DIR}/generated/${probe}.cpp")
    file(WRITE "${_probe_src}"
"// generated by tests/CMakeLists.txt — header self-containment probe\n"
"#include \"${header}\"\n"
"int main() { return 0; }\n")
    add_library(${probe} OBJECT "${_probe_src}")
    target_include_directories(${probe} PRIVATE
      ${CMAKE_SOURCE_DIR}/src
      ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_link_libraries(${probe} PRIVATE Sicnu::Geospatial)
    list(APPEND _probe_libs ${probe})
  endforeach()
  # Depend on the probe TARGETS, not $<TARGET_OBJECTS> file paths: the
  # AUTOMOC object files (mocs_compilation.cpp.o) have no forwarding rules in
  # the custom target's build.make under the Unix Makefiles generator, which
  # broke the aggregate with "no rule to make ..." (platform 10.0).
  add_custom_target(sicnu_header_probes DEPENDS ${_probe_libs})
endfunction()
sicnu_add_header_probes(
  runtime/observability/trace_id.h
  runtime/observability/trace.h
  runtime/observability/fault_point.h
  runtime/observability/fault_registry.h
  runtime/observability/diagnostic_report.h
  runtime/observability/execution_telemetry.h
  geospatial/util/gdal_compat.h
  geospatial/util/resource_uri.h
  support/bounded_fuzz.h
  contracts/text_scan.h
  contracts/contract_descriptor.h
  contracts/contract_graph.h
  contracts/operator_param_scanner.h
  contracts/command_ref_scanner.h
  contracts/error_code_scanner.h
  contracts/graph_assembly.h
  # fabric-10: data fabric / data cube module headers.
  geospatial/fabric/object_store.h
  geospatial/fabric/catalog_service.h
  geospatial/fabric/virtual_cube.h
  geospatial/fabric/chunk_plan.h
  geospatial/fabric/query_planner.h
  geospatial/fabric/prefetch.h
  geospatial/fabric/mirror.h
)

# Trace chain (task F) + store fault contracts (task E), Verification
# Platform 8.0: OutputCommitter/DatasetStore/ExperimentStore trace records +
# truthful injected-commit-failure contracts. Heavy closure (committer pulls
# the Data Manager; workflow pulls TaskCenter) — same shape as test_fault_matrix.
sicnu_add_test(test_trace_chain_8 TIMEOUT 300)
target_link_libraries(test_trace_chain_8 PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  sicnu_processing
  sicnu_data
  Sicnu::dataset
  sicnu_experiment
  sicnu_runtime
)
target_include_directories(test_trace_chain_8 PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/stubs
  ${CMAKE_BINARY_DIR}
)

# Known-answer corpus expansion (task C), Verification Platform 8.0: grid
# window/block algebra + dataset split largest-remainder and block-atomicity
# invariants. Light lane (Catch2 + Sicnu::Geospatial + Sicnu::dataset).
add_executable(test_known_answer_corpus_8
  test_known_answer_corpus_8.cpp
)
target_link_libraries(test_known_answer_corpus_8 PRIVATE
  Catch2::Catch2WithMain
  Sicnu::Geospatial
  Sicnu::dataset
  Qt6::Core
)
target_include_directories(test_known_answer_corpus_8 PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
target_compile_definitions(test_known_answer_corpus_8 PRIVATE
  CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
sicnu_discover_tests(test_known_answer_corpus_8 TIMEOUT 180)

# Bounded contract fuzz (task D), Verification Platform 8.0: worker protocol
# frames + plugin manifests + split configs. Light closure — worker_protocol
# is header-only (sicnu_runtime include), plugin_manifest.cpp compiles in
# Qt-free, splits come from Sicnu::dataset.
add_executable(test_contract_fuzz_ipc
  test_contract_fuzz_ipc.cpp
  ${CMAKE_SOURCE_DIR}/src/sdk/exprs/plugin_manifest.cpp
  ${CMAKE_SOURCE_DIR}/src/sdk/exprs/plugin_permissions.cpp
)
target_link_libraries(test_contract_fuzz_ipc PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Sicnu::dataset
)
target_include_directories(test_contract_fuzz_ipc PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/sdk
  ${CMAKE_CURRENT_SOURCE_DIR}
)
if(TARGET jsoncpp)
  target_link_libraries(test_contract_fuzz_ipc PRIVATE jsoncpp)
elseif(JSONCPP_LIBRARIES)
  target_include_directories(test_contract_fuzz_ipc SYSTEM PRIVATE ${JSONCPP_INCLUDE_DIRS})
  target_link_libraries(test_contract_fuzz_ipc PRIVATE ${JSONCPP_LIBRARIES})
else()
  target_link_libraries(test_contract_fuzz_ipc PRIVATE jsoncpp)
endif()
sicnu_discover_tests(test_contract_fuzz_ipc TIMEOUT 180)

# Bounded contract fuzz (task D), Verification Platform 8.0: operator schema
# builders + parameter projection + model manifest validation. Heavy closure
# (ModelCatalog) — full-tree lane, same shape as test_contract_fuzz_agent.
sicnu_add_test(test_contract_fuzz_ops TIMEOUT 300)
target_link_libraries(test_contract_fuzz_ops PRIVATE
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
target_include_directories(test_contract_fuzz_ops PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/processing
  ${CMAKE_CURRENT_SOURCE_DIR}
)

# Scale benchmarks (task G), Verification Platform 8.0: JobEngine 1k/10k/100k
# scheduling, DatasetStore 100k metadata, RasterReader window reads, trace
# file-sink overhead. Evidence artifact via `benchmark_scale8 --out <json>`.
add_executable(benchmark_scale8
  benchmark_scale8.cpp
)
target_link_libraries(benchmark_scale8 PRIVATE
  Qt6::Core
  GDAL::GDAL
  sicnu_jobs
  sicnu_operators_core
  sicnu_geospatial
  Sicnu::dataset
  sicnu_runtime
)
if(TARGET jsoncpp)
  target_link_libraries(benchmark_scale8 PRIVATE jsoncpp)
elseif(JSONCPP_LIBRARIES)
  target_include_directories(benchmark_scale8 SYSTEM PRIVATE ${JSONCPP_INCLUDE_DIRS})
  target_link_libraries(benchmark_scale8 PRIVATE ${JSONCPP_LIBRARIES})
else()
  target_link_libraries(benchmark_scale8 PRIVATE jsoncpp)
endif()
target_include_directories(benchmark_scale8 PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/processing
  ${CMAKE_CURRENT_SOURCE_DIR}
)

# D4 Lab Auto-Grading (ADR 0146): rule-DSL validation, byte-identical
# determinism, wrong-answer discrimination vs the committed corpus, reference
# corpus scoring, and windowed memory-budget behaviour — all through the
# OutputVerifier::gradeArtifact teaching seam.
sicnu_add_test(test_lab_grading TIMEOUT 300)
sicnu_link_jsoncpp(test_lab_grading)

# D7 batch grading: streaming engine + CSV export over the gradeArtifact
# seam; CLI-level contract for --batch (error isolation, BOM, schema).
sicnu_add_test(test_lab_batch TIMEOUT 300)
sicnu_link_jsoncpp(test_lab_batch)
target_link_libraries(test_lab_batch PRIVATE sicnu_lab_batch)

# teaching-lab-platform-11: sicnu.lab-pack/1 data pack contract (load + verify).
sicnu_add_test(test_lab_data_pack TIMEOUT 300)
sicnu_link_jsoncpp(test_lab_data_pack)

# teaching-lab-platform-11: batch classroom v2 (identity, roster, caps, cancel,
# deterministic summaries).
sicnu_add_test(test_lab_batch_v2 TIMEOUT 300)
sicnu_link_jsoncpp(test_lab_batch_v2)
target_link_libraries(test_lab_batch_v2 PRIVATE sicnu_lab_batch)

# classroom-safety 13.0: in-process CSV formula-injection neutralization,
# typed-unavailable grading (never a fabricated zero) and course-scale
# invariants. The corpus it replays is generated by scripts/gen_csv_golden.py.
sicnu_add_test(test_lab_batch_classroom_safety TIMEOUT 300)
sicnu_link_jsoncpp(test_lab_batch_classroom_safety)
target_link_libraries(test_lab_batch_classroom_safety PRIVATE sicnu_lab_batch)

# teaching-lab-platform-11: headless sicnu.labreport.v1 export (`lab --report`)
# and the recorded grade embedding seam.
sicnu_add_test(test_lab_report_cli TIMEOUT 300)
sicnu_link_jsoncpp(test_lab_report_cli)
target_link_libraries(test_lab_report_cli PRIVATE sicnu_lab_report)

# teaching-lab-platform-11: systematic prompt-injection / answer-leak corpus.
sicnu_add_test(test_harness_lab_injection TIMEOUT 300)
sicnu_link_jsoncpp(test_harness_lab_injection)

# teaching-lab-platform-11: `lab --self-check` environment diagnostics.
sicnu_add_test(test_lab_self_check TIMEOUT 300)
sicnu_link_jsoncpp(test_lab_self_check)
target_link_libraries(test_lab_self_check PRIVATE sicnu_lab_self_check)

# teaching-lab-platform-11: grader 2.0 kernel unit tests (independent
# in-test arithmetic oracle over raw-GDAL-written scenes).
sicnu_add_test(test_lab_grader_kernels TIMEOUT 300)
sicnu_link_jsoncpp(test_lab_grader_kernels)
target_link_libraries(test_lab_grader_kernels PRIVATE GDAL::GDAL ZLIB::ZLIB)

# teaching-lab-platform-11: classroom scale (bounded daily gate; the
# 1000-submission evidence run is opt-in via SICNU_LAB_SCALE_1000=1).
sicnu_add_test(test_lab_scale TIMEOUT 600)
sicnu_link_jsoncpp(test_lab_scale)
target_link_libraries(test_lab_scale PRIVATE GDAL::GDAL sicnu_lab_batch)

# teaching-lab-platform-11: offline classroom chain (gate + grade + batch +
# self-check + report) — the zero-network oracle.
sicnu_add_test(test_lab_offline_e2e TIMEOUT 300)
sicnu_link_jsoncpp(test_lab_offline_e2e)
target_link_libraries(test_lab_offline_e2e PRIVATE
  GDAL::GDAL
  sicnu_lab_batch
  sicnu_lab_report
  sicnu_lab_self_check
)

# teaching-lab-platform-11: deterministic grading fixture generator for the
# labspec labs (committed products under tests/fixtures/lab; osgeo-free
# regeneration path). Not a test — a corpus maintenance tool.
add_executable(lab_grading_fixture_gen EXCLUDE_FROM_ALL lab_grading_fixture_gen.cpp)
target_link_libraries(lab_grading_fixture_gen PRIVATE
  Qt6::Core
  GDAL::GDAL
  ZLIB::ZLIB
)
target_include_directories(lab_grading_fixture_gen PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)

# D8 capability knowledge layer: generator tool (data-driven sidecars +
# generated pi/knowledge pages) and the drift/coverage/composition guard.
add_executable(capability_knowledge_tool
  ${CMAKE_SOURCE_DIR}/scripts/capability_knowledge_tool.cpp
)
target_link_libraries(capability_knowledge_tool PRIVATE
  Qt6::Core
  sicnu_agent
  sicnu_operators
)
sicnu_link_jsoncpp(capability_knowledge_tool)
target_include_directories(capability_knowledge_tool PRIVATE
  ${CMAKE_SOURCE_DIR}/src
)
target_compile_definitions(capability_knowledge_tool PRIVATE
  CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

sicnu_add_test(test_capability_knowledge)
sicnu_link_jsoncpp(test_capability_knowledge)

# D6 i18n guards: .ts completeness, glossary resolution, unwrapped-string audit
add_executable(test_i18n
  test_i18n.cpp
)
target_link_libraries(test_i18n PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Xml
  Sicnu::help
)
target_include_directories(test_i18n PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
target_compile_definitions(test_i18n PRIVATE
  CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
set_target_properties(test_i18n PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_i18n)

# ── Workbench 10.0 — unified object identity + agent context projection ──
add_executable(test_object_identity
  test_object_identity.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/object_identity.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/selection_context.cpp
  # selection_context.cpp references WorkbenchHost; workbench_host.cpp is
  # Qt-only, so link the REAL implementation instead of stubbing it (the
  # out-of-tree targets were link-broken since the Workbench 5.0 seam).
  ${CMAKE_SOURCE_DIR}/src/app/workbench/workbench_host.cpp
)
target_link_libraries(test_object_identity PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_data
)
if(TARGET jsoncpp)
  target_link_libraries(test_object_identity PRIVATE jsoncpp)
endif()
target_include_directories(test_object_identity PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_object_identity PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_object_identity)

# D18 — MissionContext (Qt Core only + selection_context / object_identity)
add_executable(test_mission_context
  test_mission_context.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/mission_context.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/mission_context_store.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/object_identity.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/selection_context.cpp
)
target_link_libraries(test_mission_context PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Xml
  qgis_core
  qgis_gui
  sicnu_data
)
if(TARGET jsoncpp)
  target_link_libraries(test_mission_context PRIVATE jsoncpp)
endif()
target_include_directories(test_mission_context PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_mission_context PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_mission_context)

# Mission Runtime 13.0 single-authority persistence: load/save contract, the
# boundary-reset → fresh-mission rule the window's New/Open Project boundary
# relies on, and the poisoned-authority save refusal. Qt XML channel included.
add_executable(test_mission_runtime_store
  test_mission_runtime_store.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/mission_runtime_store.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/mission_context.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/mission_context_store.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/mission_stage.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/mission_timeline_bridge.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/mission_timeline_store.cpp
  # selection_context pulls ContextRules + WorkbenchHost (same pair as
  # test_workbench_host); mission_context pulls ContextRules helpers.
  ${CMAKE_SOURCE_DIR}/src/app/workbench/selection_context.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/workbench_host.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/adapters.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/object_identity.cpp
)
target_link_libraries(test_mission_runtime_store PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Xml
  qgis_core
  qgis_gui
  sicnu_data
)
if(TARGET jsoncpp)
  target_link_libraries(test_mission_runtime_store PRIVATE jsoncpp)
endif()
target_include_directories(test_mission_runtime_store PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_mission_runtime_store PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_mission_runtime_store)

add_executable(test_mission_e2e_scaffolding
  test_mission_e2e_scaffolding.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/mission_context.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/mission_context_store.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/object_identity.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/selection_context.cpp
)
target_link_libraries(test_mission_e2e_scaffolding PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Xml
  qgis_core
  qgis_gui
  sicnu_data
)
if(TARGET jsoncpp)
  target_link_libraries(test_mission_e2e_scaffolding PRIVATE jsoncpp)
endif()
target_include_directories(test_mission_e2e_scaffolding PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_mission_e2e_scaffolding PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_mission_e2e_scaffolding)

# ---------------------------------------------------------------------------
# Unified Mission Workbench 12.0 — mission task space gates.
#
# These four targets deliberately link Qt Core (+Xml) ONLY: the task space
# (state machine, projection, paged model, sidecar store) must stay free of
# QGIS / Qt Widgets dependencies so the stage, parity, scale and end-to-end
# gates stay cheap to build and run. The desktop wiring and the MCP tool
# provider live in the sicnu_geo_rs / sicnu_agent targets instead.
# ---------------------------------------------------------------------------
set(SICNU_MISSION_TASK_SPACE_SOURCES
  ${CMAKE_SOURCE_DIR}/src/app/workbench/mission_stage.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/mission_projection.cpp
)

function(sicnu_add_mission_test NAME)
  add_executable(${NAME} ${NAME}.cpp ${SICNU_MISSION_TASK_SPACE_SOURCES})
  target_link_libraries(${NAME} PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
  )
  target_include_directories(${NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
  )
  # The parity gate parses the real Pi bridge and MCP allow-list from the
  # source tree, so a forgotten surface wiring fails the test.
  target_compile_definitions(${NAME} PRIVATE
    SICNU_MISSION_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
  )
  set_target_properties(${NAME} PROPERTIES AUTOMOC ON)
  sicnu_discover_tests(${NAME})
endfunction()

sicnu_add_mission_test(test_mission_stage)
sicnu_add_mission_test(test_mission_surface_parity)
sicnu_add_mission_test(test_mission_scale_benchmark)


add_executable(test_agent_workbench_context
  test_agent_workbench_context.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/agent_context_tool.cpp
)
target_link_libraries(test_agent_workbench_context PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  sicnu_agent
)
if(TARGET jsoncpp)
  target_link_libraries(test_agent_workbench_context PRIVATE jsoncpp)
endif()
target_include_directories(test_agent_workbench_context PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_link_jsoncpp(test_agent_workbench_context)
set_target_properties(test_agent_workbench_context PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_agent_workbench_context)

add_executable(test_visual_analytics
  test_visual_analytics.cpp
  ${CMAKE_SOURCE_DIR}/src/app/visualanalytics/va_source.cpp
  ${CMAKE_SOURCE_DIR}/src/app/visualanalytics/va_chart_widget.cpp
  ${CMAKE_SOURCE_DIR}/src/app/visualanalytics/va_selection_hub.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/rs_scan_pool.cpp
)
target_link_libraries(test_visual_analytics PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
)
target_include_directories(test_visual_analytics PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_SOURCE_DIR}/src/app/widgets
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_visual_analytics PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_visual_analytics)

add_executable(test_view_link
  test_view_link.cpp
  ${CMAKE_SOURCE_DIR}/src/app/shell/view_link_controller.cpp
  ${CMAKE_SOURCE_DIR}/src/app/visualanalytics/va_cursor_probe.cpp
  ${CMAKE_SOURCE_DIR}/src/app/visualanalytics/va_layer_link_controller.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/rs_scan_pool.cpp
)
target_link_libraries(test_view_link PRIVATE
  Catch2::Catch2
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_data
  Sicnu::Geospatial
  sicnu_qgis_display
)
if(TARGET jsoncpp)
  target_link_libraries(test_view_link PRIVATE jsoncpp)
endif()
target_include_directories(test_view_link PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_SOURCE_DIR}/src/app/widgets
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_view_link PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_view_link)

add_executable(test_processing_catalog_ux
  test_processing_catalog_ux.cpp
  ${CMAKE_SOURCE_DIR}/src/app/shell/rs_operator_catalog_panel.cpp
)
target_link_libraries(test_processing_catalog_ux PRIVATE
  Catch2::Catch2
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_data
  sicnu_agent
  sicnu_operators
  sicnu_processing
)
if(TARGET jsoncpp)
  target_link_libraries(test_processing_catalog_ux PRIVATE jsoncpp)
endif()
target_include_directories(test_processing_catalog_ux PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_processing_catalog_ux PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_processing_catalog_ux)

  # ---- D16 Temporal Phenology Timeline Studio ----------------------------
  # Light-pattern tests: link sicnu_temporal_timeline (+ Sicnu::Geospatial,
  # GDAL for runtime fixtures) — no qgis_core/sicnu_processing in the chain.
  add_executable(test_virtual_cube_memory test_virtual_cube_memory.cpp)
  target_link_libraries(test_virtual_cube_memory PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    sicnu_temporal_timeline
    Sicnu::Geospatial
    GDAL::GDAL
  )
  target_include_directories(test_virtual_cube_memory PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
  )
  target_compile_definitions(test_virtual_cube_memory PRIVATE
    CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    CMAKE_BINARY_DIR="${CMAKE_BINARY_DIR}"
  )
  # D16 acceptance gate registers the whole binary under its target
  # name (ctest -R test_d16_|... matches verbatim, -j1, headless).
  add_test(NAME test_virtual_cube_memory COMMAND test_virtual_cube_memory)
  set_tests_properties(test_virtual_cube_memory PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 600)

  add_executable(test_whittaker_smooth test_whittaker_smooth.cpp)
  target_link_libraries(test_whittaker_smooth PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    sicnu_temporal_timeline
  )
  target_include_directories(test_whittaker_smooth PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
  )
  # D16 acceptance gate registers the whole binary under its target
  # name (ctest -R test_d16_|... matches verbatim, -j1, headless).
  add_test(NAME test_whittaker_smooth COMMAND test_whittaker_smooth)
  set_tests_properties(test_whittaker_smooth PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 600)

  add_executable(test_phenology_extraction test_phenology_extraction.cpp)
  target_link_libraries(test_phenology_extraction PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    sicnu_temporal_timeline
  )
  target_include_directories(test_phenology_extraction PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
  )
  # D16 acceptance gate registers the whole binary under its target
  # name (ctest -R test_d16_|... matches verbatim, -j1, headless).
  add_test(NAME test_phenology_extraction COMMAND test_phenology_extraction)
  set_tests_properties(test_phenology_extraction PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 600)

  add_executable(test_bfast_harmonic_breaks test_bfast_harmonic_breaks.cpp)
  target_link_libraries(test_bfast_harmonic_breaks PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    sicnu_temporal_timeline
  )
  target_include_directories(test_bfast_harmonic_breaks PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
  )
  # D16 acceptance gate registers the whole binary under its target
  # name (ctest -R test_d16_|... matches verbatim, -j1, headless).
  add_test(NAME test_bfast_harmonic_breaks COMMAND test_bfast_harmonic_breaks)
  set_tests_properties(test_bfast_harmonic_breaks PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 600)

  add_executable(test_d16_temporal_trend test_d16_temporal_trend.cpp)
  target_link_libraries(test_d16_temporal_trend PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    sicnu_temporal_timeline
  )
  target_include_directories(test_d16_temporal_trend PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
  )
  # D16 acceptance gate registers the whole binary under its target
  # name (ctest -R test_d16_|... matches verbatim, -j1, headless).
  add_test(NAME test_d16_temporal_trend COMMAND test_d16_temporal_trend)
  set_tests_properties(test_d16_temporal_trend PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 600)

  add_executable(test_d16_starfm test_d16_starfm.cpp)
  target_link_libraries(test_d16_starfm PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    sicnu_temporal_timeline
  )
  target_include_directories(test_d16_starfm PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
  )
  # D16 acceptance gate registers the whole binary under its target
  # name (ctest -R test_d16_|... matches verbatim, -j1, headless).
  add_test(NAME test_d16_starfm COMMAND test_d16_starfm)
  set_tests_properties(test_d16_starfm PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 600)

  # D16 Package G: timeline scrubber + temporal profile (offscreen Qt).
  # Spec file name kept (test_timeline_scrubber_widget.cpp); the target
  # carries the test_d16_ prefix so the acceptance filter gates it.
  add_executable(test_d16_timeline_scrubber_widget test_timeline_scrubber_widget.cpp)
  target_link_libraries(test_d16_timeline_scrubber_widget PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    sicnu_temporal_timeline
  )
  target_include_directories(test_d16_timeline_scrubber_widget PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
  )
  set_target_properties(test_d16_timeline_scrubber_widget PROPERTIES AUTOMOC ON)
  add_test(NAME test_d16_timeline_scrubber_widget COMMAND test_d16_timeline_scrubber_widget)
  set_tests_properties(test_d16_timeline_scrubber_widget PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 600)

  add_executable(test_d16_temporal_profile_widget test_temporal_profile_widget.cpp)
  target_link_libraries(test_d16_temporal_profile_widget PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    sicnu_temporal_timeline
  )
  target_include_directories(test_d16_temporal_profile_widget PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
  )
  set_target_properties(test_d16_temporal_profile_widget PROPERTIES AUTOMOC ON)
  add_test(NAME test_d16_temporal_profile_widget COMMAND test_d16_temporal_profile_widget)
  set_tests_properties(test_d16_temporal_profile_widget PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 600)

  add_executable(test_d16_temporal_tools test_d16_temporal_tools.cpp)
  target_link_libraries(test_d16_temporal_tools PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    sicnu_temporal_timeline
  )
  target_include_directories(test_d16_temporal_tools PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
  )
  sicnu_link_jsoncpp(test_d16_temporal_tools)
  # D16 acceptance gate registers the whole binary under its target
  # name (ctest -R test_d16_|... matches verbatim, -j1, headless).
  add_test(NAME test_d16_temporal_tools COMMAND test_d16_temporal_tools)
  set_tests_properties(test_d16_temporal_tools PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 600)

  add_executable(test_d16_temporal_phenology_e2e test_d16_temporal_phenology_e2e.cpp)
  target_link_libraries(test_d16_temporal_phenology_e2e PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    sicnu_temporal_timeline
  )
  target_include_directories(test_d16_temporal_phenology_e2e PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
  )
  target_compile_definitions(test_d16_temporal_phenology_e2e PRIVATE
    CMAKE_BINARY_DIR="${CMAKE_BINARY_DIR}"
  )
  sicnu_link_jsoncpp(test_d16_temporal_phenology_e2e)
  # D16 acceptance gate registers the whole binary under its target
  # name (ctest -R test_d16_|... matches verbatim, -j1, headless).
  add_test(NAME test_d16_temporal_phenology_e2e COMMAND test_d16_temporal_phenology_e2e)
  set_tests_properties(test_d16_temporal_phenology_e2e PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 600)
# ---- D14: geometric registration workbench (ADR 0159) ----
sicnu_add_test(test_gcp_manager)
sicnu_add_test(test_geometric_transform)
sicnu_add_test(test_tps_interpolator)
sicnu_add_test(test_feature_matcher)
sicnu_add_test(test_resampler)
sicnu_add_test(test_pansharpening)

# D14 Package G: dual-window georeferencing workbench (app sources compiled in).
add_executable(test_georef_dual_window_workbench
  test_georef_dual_window_workbench.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/georef_dual_window.cpp
  ${CMAKE_SOURCE_DIR}/src/app/map_tools/swipe_map_tool.cpp
)
target_link_libraries(test_georef_dual_window_workbench PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
  qgis_core
  qgis_gui
  sicnu_processing
)
target_include_directories(test_georef_dual_window_workbench PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_georef_dual_window_workbench PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_georef_dual_window_workbench)
sicnu_add_test(test_geometric_agent_tools)
sicnu_add_test(test_d14_geometric_registration_e2e)
# ---- F13: multimodal registration & geometric accuracy ----
sicnu_add_test(test_registration_fft)
sicnu_add_test(test_multimodal_matcher)
sicnu_add_test(test_model_selector)
sicnu_add_test(test_rpc_bias_model)
sicnu_add_test(test_stack_registrator)
sicnu_add_test(test_registration_quality)
sicnu_add_test(test_registration_operators)
sicnu_add_test(test_registration_e2e)
# F13 #1005 fail-closed pick transform (app source compiled in).
add_executable(test_georef_crs_pick_failclosed
  test_georef_crs_pick_failclosed.cpp
  ${CMAKE_SOURCE_DIR}/src/app/georeferencer/rs_georef_crs_pick.cpp
)
target_link_libraries(test_georef_crs_pick_failclosed PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
)
target_include_directories(test_georef_crs_pick_failclosed PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_SOURCE_DIR}/src/app/georeferencer
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_georef_crs_pick_failclosed PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_georef_crs_pick_failclosed)
# =============================================================================
# D17 — workflow pipeline designer (ADR 0162)
# =============================================================================
# The D17 engine sources call SICNU_FAULT_POINT (workflow_checkpoint.cpp,
# pipeline_run_coordinator.cpp); the Qt-free leaf registry is compiled into
# the light targets directly so they stay independent of sicnu_runtime.
set(SICNU_FAULT_REGISTRY_SOURCE
  ${CMAKE_SOURCE_DIR}/src/runtime/observability/fault_registry.cpp
)
# D17 test targets are deliberately MINIMAL: Catch2 + Qt (Core/Gui/Widgets as
# needed) + jsoncpp where the LabSpec loader is compiled in. They do not link
# the qgis/processing/task_center chain, so the whole designer suite builds
# and runs hermetically (ADR 0162). Widget/engine sources are compiled into
# the test targets — the established repo pattern for UI suites.
function(sicnu_add_d17_test NAME)
  cmake_parse_arguments(D17 "" "" "SOURCES;LIBS" ${ARGN})
  add_executable(${NAME} ${NAME}.cpp ${D17_SOURCES})
  target_link_libraries(${NAME} PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    Qt6::Gui
    ${D17_LIBS}
  )
  target_include_directories(${NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/app
    ${CMAKE_BINARY_DIR}
  )
  target_compile_definitions(${NAME} PRIVATE
    CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
  )
  set_target_properties(${NAME} PROPERTIES AUTOMOC ON)
  sicnu_discover_tests(${NAME})
endfunction()

set(D17_ENGINE_SOURCES
  ${CMAKE_SOURCE_DIR}/src/workflow/workflow_ir_v2.cpp
  ${CMAKE_SOURCE_DIR}/src/workflow/workflow_dag_analyzer.cpp
)
set(D17_CANVAS_SOURCES
  ${CMAKE_SOURCE_DIR}/src/app/pipeline/pipeline_canvas_widget.cpp
  ${CMAKE_SOURCE_DIR}/src/app/pipeline/pipeline_scene.cpp
  ${CMAKE_SOURCE_DIR}/src/app/pipeline/pipeline_node_item.cpp
  ${CMAKE_SOURCE_DIR}/src/app/pipeline/pipeline_port_item.cpp
  ${CMAKE_SOURCE_DIR}/src/app/pipeline/pipeline_connection_item.cpp
)

# --- Package A: Workflow IR 2.0
sicnu_add_d17_test(test_workflow_ir_v2
  SOURCES ${CMAKE_SOURCE_DIR}/src/workflow/workflow_ir_v2.cpp
)

# --- Package B: DAG analyzer
sicnu_add_d17_test(test_workflow_dag_analysis
  SOURCES ${D17_ENGINE_SOURCES}
)

# --- Package B2: subflow composition (WP2)
sicnu_add_d17_test(test_workflow_composition
  SOURCES ${D17_ENGINE_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/workflow/workflow_composer.cpp
    ${CMAKE_SOURCE_DIR}/src/workflow/plan_optimizer.cpp
    ${CMAKE_SOURCE_DIR}/src/workflow/workflow_cost_estimator.cpp
)

# --- Package C: contract checker + repair engine
sicnu_add_d17_test(test_workflow_repair_rules
  SOURCES ${D17_ENGINE_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/workflow/contract_checker.cpp
    ${CMAKE_SOURCE_DIR}/src/workflow/workflow_repair_engine.cpp
)

# --- Package D: plan optimizer + cost estimator
sicnu_add_d17_test(test_workflow_cost_estimator
  SOURCES ${D17_ENGINE_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/workflow/plan_optimizer.cpp
    ${CMAKE_SOURCE_DIR}/src/workflow/workflow_cost_estimator.cpp
)

# --- D18: IR2 port→param mapping + registry executor honesty (#1002/#1006)
sicnu_add_d17_test(test_ir2_port_param_mapping
  SOURCES ${D17_ENGINE_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/workflow/plan_optimizer.cpp
    ${CMAKE_SOURCE_DIR}/src/workflow/workflow_cost_estimator.cpp
    ${CMAKE_SOURCE_DIR}/src/workflow/workflow_composer.cpp
    ${CMAKE_SOURCE_DIR}/src/workflow/workflow_provenance.cpp
    ${CMAKE_SOURCE_DIR}/src/workflow/pipeline_run_coordinator.cpp
    ${CMAKE_SOURCE_DIR}/src/workflow/ir2_port_param_mapping.cpp
    ${CMAKE_SOURCE_DIR}/src/workflow/ir2_registry_node_executor.cpp
    ${SICNU_FAULT_REGISTRY_SOURCE}
  LIBS Qt6::Widgets Qt6::Test jsoncpp sicnu_operators
)

# --- Package E: run coordinator + checkpoints
sicnu_add_d17_test(test_workflow_checkpoint_cache
  SOURCES ${D17_ENGINE_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/workflow/plan_optimizer.cpp
    ${CMAKE_SOURCE_DIR}/src/workflow/workflow_cost_estimator.cpp
    ${CMAKE_SOURCE_DIR}/src/workflow/workflow_composer.cpp
    ${CMAKE_SOURCE_DIR}/src/workflow/workflow_provenance.cpp
    ${CMAKE_SOURCE_DIR}/src/workflow/pipeline_run_coordinator.cpp
    ${SICNU_FAULT_REGISTRY_SOURCE}
  LIBS Qt6::Widgets Qt6::Test
)

# --- Package F: canvas
sicnu_add_d17_test(test_pipeline_canvas_widget
  SOURCES ${D17_CANVAS_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/workflow/workflow_ir_v2.cpp
  LIBS Qt6::Widgets
)

# --- Package G: guided workbench (dual view)
sicnu_add_d17_test(test_guided_workflow_sync
  SOURCES ${D17_CANVAS_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/app/pipeline/guided_workflow_workbench.cpp
    ${CMAKE_SOURCE_DIR}/src/app/pipeline/labspec_workflow_lift.cpp
    ${CMAKE_SOURCE_DIR}/src/app/widgets/lab_spec_loader.cpp
    ${CMAKE_SOURCE_DIR}/src/workflow/workflow_ir_v2.cpp
    ${CMAKE_SOURCE_DIR}/src/workflow/workflow_dag_analyzer.cpp
  LIBS Qt6::Widgets Qt6::Test jsoncpp
)

# --- Package H: agent orchestrator tool
sicnu_add_d17_test(test_workflow_agent_tools
  SOURCES ${D17_ENGINE_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/workflow/contract_checker.cpp
    ${CMAKE_SOURCE_DIR}/src/workflow/workflow_repair_engine.cpp
    ${CMAKE_SOURCE_DIR}/src/agent/tools/workflow_orchestrator_tool.cpp
)

# --- Package I: end-to-end (labs corpus needs the LabSpec loader -> jsoncpp)
sicnu_add_d17_test(test_d17_workflow_pipeline_e2e
  SOURCES ${D17_ENGINE_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/workflow/plan_optimizer.cpp
    ${CMAKE_SOURCE_DIR}/src/workflow/workflow_cost_estimator.cpp
    ${CMAKE_SOURCE_DIR}/src/workflow/workflow_composer.cpp
    ${CMAKE_SOURCE_DIR}/src/workflow/workflow_provenance.cpp
    ${CMAKE_SOURCE_DIR}/src/workflow/pipeline_run_coordinator.cpp
    ${CMAKE_SOURCE_DIR}/src/app/pipeline/labspec_workflow_lift.cpp
    ${CMAKE_SOURCE_DIR}/src/app/widgets/lab_spec_loader.cpp
    ${SICNU_FAULT_REGISTRY_SOURCE}
  LIBS Qt6::Widgets Qt6::Test jsoncpp
)

# Targets that compile pipeline_run_coordinator.cpp directly (instead of
# linking sicnu_workflow) must define the library's EXPORTS macro so
# SICNU_WORKFLOW_EXPORT resolves to dllexport -- otherwise the local moc
# output defines PipelineRunCoordinator::staticMetaObject as a dllimport
# data member, which MSVC rejects (C2491).
foreach(_d17_embeds_coordinator
    test_ir2_port_param_mapping
    test_workflow_checkpoint_cache
    test_d17_workflow_pipeline_e2e)
  target_compile_definitions(${_d17_embeds_coordinator} PRIVATE sicnu_workflow_EXPORTS)
endforeach()

# --- Track 13 (workflow durability): Engine-2.0 lineage envelope, checkpoint
# election, legacy migration and runId fuzz. Light lane: the checkpoint/run
# sources are compiled directly so the suite stays independent of the
# qgis/task_center chain (same pattern as the D17 targets above).
add_executable(test_workflow_durability_13 test_workflow_durability_13.cpp
  ${CMAKE_SOURCE_DIR}/src/workflow/workflow_definition.cpp
  ${CMAKE_SOURCE_DIR}/src/workflow/placeholder_grammar.cpp
  ${CMAKE_SOURCE_DIR}/src/workflow/workflow_run.cpp
  ${CMAKE_SOURCE_DIR}/src/workflow/workflow_checkpoint.cpp
  ${CMAKE_SOURCE_DIR}/src/workflow/workflow_run_lock.cpp
  ${SICNU_FAULT_REGISTRY_SOURCE}
)
target_link_libraries(test_workflow_durability_13 PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Network
  jsoncpp
)
target_include_directories(test_workflow_durability_13 PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_workflow_durability_13)
# ---------------------------------------------------------------------------
# D15 classification-change-studio track (Packages A-I).
# ---------------------------------------------------------------------------

# Package A: spatial leakage defense (block split + Moran's I audit).
sicnu_add_test(test_spatial_block_leakage TIMEOUT 120 TEST_PREFIX "test_spatial_block_leakage::")

# Package B: GLCM texture features.
sicnu_add_test(test_glcm_texture TIMEOUT 120 TEST_PREFIX "test_glcm_texture::")

# Package C: classifier kernels (KMeans/ISODATA/RF/SVM/NormalBayes).
sicnu_add_test(test_classifier_engine TIMEOUT 300 TEST_PREFIX "test_classifier_engine::")

# Package D: post-classification morphology.
sicnu_add_test(test_classification_postprocess TIMEOUT 120 TEST_PREFIX "test_classification_postprocess::")

# F12: object-level thematic cleanup (segment adjacency, min-area, smoothing).
# Direct link (sicnu_processing only) — the module is pure STL and does not
# need the sicnu_add_test full stack, which is currently unbuildable on this
# host toolchain by two PRE-EXISTING master compile errors unrelated to F12
# (see .planning/classification-intelligence-11/EVIDENCE.md).
add_executable(test_classification_object_postprocess test_classification_object_postprocess.cpp)
target_link_libraries(test_classification_object_postprocess PRIVATE
  Catch2::Catch2WithMain
  qgis_core
  sicnu_processing
  Qt6::Core
)
target_include_directories(test_classification_object_postprocess PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/processing/algorithms
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_classification_object_postprocess TIMEOUT 120)

# F12: spatially aware k-fold CV with leakage audit (qgis_analysis + OpenCV).
add_executable(test_spatial_cross_validation test_spatial_cross_validation.cpp)
target_link_libraries(test_spatial_cross_validation PRIVATE
  Catch2::Catch2WithMain
  qgis_core
  qgis_analysis
  ${OpenCV_LIBS}
)
target_include_directories(test_spatial_cross_validation PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/classification
  ${OpenCV_INCLUDE_DIRS}
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_spatial_cross_validation)

# F12: end-to-end chains (uncertainty raster semantics, sidecar v2 replay,
# v1 compatibility, imbalanced classes).
add_executable(test_classification_intelligence_e2e test_classification_intelligence_e2e.cpp)
target_link_libraries(test_classification_intelligence_e2e PRIVATE
  Catch2::Catch2WithMain
  qgis_core
  qgis_analysis
  GDAL::GDAL
  ${OpenCV_LIBS}
)
target_include_directories(test_classification_intelligence_e2e PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/classification
  ${OpenCV_INCLUDE_DIRS}
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_classification_intelligence_e2e TIMEOUT 300)

# F12: bounded logical-scale evidence (100k samples; invariants only).
add_executable(test_classification_intelligence_scale test_classification_intelligence_scale.cpp)
target_link_libraries(test_classification_intelligence_scale PRIVATE
  Catch2::Catch2WithMain
  qgis_core
  qgis_analysis
  ${OpenCV_LIBS}
)
target_include_directories(test_classification_intelligence_scale PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/classification
  ${OpenCV_INCLUDE_DIRS}
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_classification_intelligence_scale RUN_SERIAL TIMEOUT 600)

# Packages E/F: pixel change detection + accuracy assessment.
sicnu_add_test(test_change_detector TIMEOUT 120 TEST_PREFIX "test_change_detector::")
sicnu_add_test(test_confusion_matrix TIMEOUT 120 TEST_PREFIX "test_confusion_matrix::")

# Package H: agent classification diagnostics.
sicnu_add_test(test_classification_agent_tools TIMEOUT 120 TEST_PREFIX "test_classification_agent_tools::")

# Package G: studio workbench (widget translation unit is compiled into the
# test executables, mirroring the test_view_link pattern; the app target
# also lists the TU).
foreach(_d15_g_test test_magic_wand_roi test_feature_scatter test_classification_studio_widget)
  add_executable(${_d15_g_test}
    ${_d15_g_test}.cpp
    ${CMAKE_SOURCE_DIR}/src/app/workbench/classification_studio_widget.cpp
  )
  target_link_libraries(${_d15_g_test} PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Test
    qgis_core
    qgis_gui
    GDAL::GDAL
  )
  target_include_directories(${_d15_g_test} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/app
    ${CMAKE_BINARY_DIR}
  )
  set_target_properties(${_d15_g_test} PROPERTIES AUTOMOC ON)
  sicnu_discover_tests(${_d15_g_test} TIMEOUT 120 TEST_PREFIX "${_d15_g_test}::")
endforeach()

# Package I: full-pipeline E2E + lab grading contracts (needs the agent
# grader seam, jsoncpp for the OutputVerifier header, GDAL for synthesis).
add_executable(test_d15_classification_change_e2e
  test_d15_classification_change_e2e.cpp
  ${CMAKE_SOURCE_DIR}/tests/support/d15_e2e_pipeline.cpp
)
target_link_libraries(test_d15_classification_change_e2e PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  sicnu_processing
  sicnu_agent
  GDAL::GDAL
)
if(TARGET jsoncpp)
  target_link_libraries(test_d15_classification_change_e2e PRIVATE jsoncpp)
endif()
target_include_directories(test_d15_classification_change_e2e PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/tests
  ${CMAKE_BINARY_DIR}
)
target_compile_definitions(test_d15_classification_change_e2e PRIVATE
  CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
sicnu_discover_tests(test_d15_classification_change_e2e TIMEOUT 600 TEST_PREFIX "test_d15_classification_change_e2e::")

# Execution Runtime Convergence 11.0 — authority gate, unified chunk contract,
# crash-safe resume, resource governor, worker lease, per-chunk telemetry,
# adoption kit, scale/fault matrix (all link the world via sicnu_add_test).
sicnu_add_test(test_execution_authority_11 TIMEOUT 300)
target_link_libraries(test_execution_authority_11 PRIVATE sicnu_runtime)
sicnu_add_test(test_chunk_contract_11 TIMEOUT 120)
target_link_libraries(test_chunk_contract_11 PRIVATE sicnu_runtime)
sicnu_add_test(test_chunk_resume_11 TIMEOUT 300)
target_link_libraries(test_chunk_resume_11 PRIVATE sicnu_runtime)
sicnu_add_test(test_execution_governor_11 TIMEOUT 180)
target_link_libraries(test_execution_governor_11 PRIVATE sicnu_runtime)
sicnu_add_test(test_worker_lease_11 TIMEOUT 120)
target_link_libraries(test_worker_lease_11 PRIVATE sicnu_runtime)
sicnu_add_test(test_execution_telemetry_11 TIMEOUT 180)
target_link_libraries(test_execution_telemetry_11 PRIVATE sicnu_runtime)
sicnu_add_test(test_chunk_adoption_11 TIMEOUT 300)
target_link_libraries(test_chunk_adoption_11 PRIVATE sicnu_runtime)
sicnu_add_test(test_execution_scale_fault_11 TIMEOUT 600)
target_link_libraries(test_execution_scale_fault_11 PRIVATE sicnu_runtime)
# ---- F15: large-scale mosaic, fusion & quality composite (mosaic-fusion-11) ----
# Algorithm-layer tests: link sicnu_processing (+GDAL for fixture rasters and
# OGR CRS oracles) without the full operator/agent chain.
add_executable(test_mosaic_plan test_mosaic_plan.cpp)
target_link_libraries(test_mosaic_plan PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  sicnu_processing
  GDAL::GDAL
)
target_include_directories(test_mosaic_plan PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_mosaic_plan TEST_PREFIX "test_mosaic_plan::")

# F15: mosaic/fusion algorithm kernels — balancing, seamline, blend, quality,
# fusion quality report.
foreach(_f15_test
    test_mosaic_balancing
    test_mosaic_seamline
    test_mosaic_blend
    test_mosaic_quality
    test_fusion_quality_report
    test_mosaic_scale)
  add_executable(${_f15_test} ${_f15_test}.cpp)
  target_link_libraries(${_f15_test} PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    qgis_core
    sicnu_processing
    GDAL::GDAL
  )
  if(TARGET jsoncpp)
    target_link_libraries(${_f15_test} PRIVATE jsoncpp)
  endif()
  target_include_directories(${_f15_test} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/tests
    ${CMAKE_BINARY_DIR}
  )
  sicnu_discover_tests(${_f15_test} TEST_PREFIX "${_f15_test}::")
endforeach()

# F15: rs:quality_mosaic registry-path wiring test.
add_executable(test_quality_mosaic_registration test_quality_mosaic_registration.cpp)
target_link_libraries(test_quality_mosaic_registration PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  sicnu_processing
  sicnu_operators
  GDAL::GDAL
)
if(TARGET jsoncpp)
  target_link_libraries(test_quality_mosaic_registration PRIVATE jsoncpp)
endif()
target_include_directories(test_quality_mosaic_registration PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_quality_mosaic_registration TEST_PREFIX "test_quality_mosaic_registration::")

# F15: rs:quality_mosaic operator E2E (GDAL fixtures, provenance, report).
add_executable(test_quality_mosaic_operator test_quality_mosaic_operator.cpp)
target_link_libraries(test_quality_mosaic_operator PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  sicnu_processing
  sicnu_operators
  GDAL::GDAL
)
if(TARGET jsoncpp)
  target_link_libraries(test_quality_mosaic_operator PRIVATE jsoncpp)
endif()
target_include_directories(test_quality_mosaic_operator PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/tests
  ${CMAKE_BINARY_DIR}
)
target_compile_definitions(test_quality_mosaic_operator PRIVATE
  CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
sicnu_discover_tests(test_quality_mosaic_operator TIMEOUT 300 TEST_PREFIX "test_quality_mosaic_operator::")
# ─── D13 · Radiometric Spectral Workbench ──────────────────────────────────
# Radiometric unit FSM (ADR 0158): transition law against the hand-written
# ADR truth table, preflight exception blocking, custom-property/GDAL
# metadata round-trip, fail-safe DN default.
qt_add_executable(test_radiometric_state
  test_radiometric_state.cpp
)
target_link_libraries(test_radiometric_state PRIVATE
  Catch2::Catch2
  Qt6::Core
  qgis_core
  sicnu_core
)
target_include_directories(test_radiometric_state PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_radiometric_state)

# 6S-style LUT atmospheric correction: closed-form inversion truth, physics
# invariants, DOS2 hand-worked arithmetic, sentinel discipline.
qt_add_executable(test_fast_6s_atmospheric
  test_fast_6s_atmospheric.cpp
)
target_link_libraries(test_fast_6s_atmospheric PRIVATE
  Catch2::Catch2WithMain
  sicnu_spectral_analysis
)
target_include_directories(test_fast_6s_atmospheric PRIVATE
  ${CMAKE_SOURCE_DIR}/src
)
sicnu_discover_tests(test_fast_6s_atmospheric)

# Hyperspectral continuum removal: Gaussian closed-form truths (depth, FWHM,
# area), hull-vertex normalization, multi-trough isolation, invalid-grid guards.
qt_add_executable(test_continuum_removal
  test_continuum_removal.cpp
)
target_link_libraries(test_continuum_removal PRIVATE
  Catch2::Catch2WithMain
  sicnu_spectral_analysis
)
target_include_directories(test_continuum_removal PRIVATE
  ${CMAKE_SOURCE_DIR}/src
)
sicnu_discover_tests(test_continuum_removal)

# FCLS unmixing + endmember extraction (D13): synthetic mixture ground truth,
# simplex feasibility, degenerate refusals, PPI/VCA set recovery.
qt_add_executable(test_spectral_unmixing_fcls
  test_spectral_unmixing_fcls.cpp
)
target_link_libraries(test_spectral_unmixing_fcls PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  sicnu_processing
)
target_include_directories(test_spectral_unmixing_fcls PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_spectral_unmixing_fcls)

# Agent spectral tools (D13): BOA physics audit laws, wavelength-driven band
# selection refusal, spectral inspect extraction + library match.
qt_add_executable(test_spectral_agent_tools
  test_spectral_agent_tools.cpp
)
target_link_libraries(test_spectral_agent_tools PRIVATE
  Catch2::Catch2
  Qt6::Core
  qgis_core
  sicnu_agent
  sicnu_processing
)
if(TARGET jsoncpp)
  target_link_libraries(test_spectral_agent_tools PRIVATE jsoncpp)
endif()
target_include_directories(test_spectral_agent_tools PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_spectral_agent_tools PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_spectral_agent_tools)

# D13 end-to-end integration: DN→calibration→6S→BOA→indices→continuum→FCLS→
# library, plus the Lab02/Lab08 100-point rubric grading and the uncalibrated
# DN injection penalty contract (exactly −30).
qt_add_executable(test_d13_radiometric_spectral_e2e
  test_d13_radiometric_spectral_e2e.cpp
)
target_link_libraries(test_d13_radiometric_spectral_e2e PRIVATE
  Catch2::Catch2
  Qt6::Core
  Qt6::Gui
  qgis_core
  sicnu_core
  sicnu_processing
  sicnu_spectral_analysis
  GDAL::GDAL
)
target_include_directories(test_d13_radiometric_spectral_e2e PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_d13_radiometric_spectral_e2e PROPERTIES AUTOMOC ON)
target_compile_definitions(test_d13_radiometric_spectral_e2e PRIVATE
  SICNU_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
sicnu_discover_tests(test_d13_radiometric_spectral_e2e)
# F11 qgis-editing-annotation-11: editing platform tests. App TUs under test
# are compiled into the binaries (test_view_link / D15 pattern); only core is
# linked for the session suite, gui for the canvas-driven suites.
add_executable(test_edit_session
  test_edit_session.cpp
  ${CMAKE_SOURCE_DIR}/src/app/editing/rs_edit_session.cpp
)
target_link_libraries(test_edit_session PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
)
target_include_directories(test_edit_session PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_edit_session PROPERTIES AUTOMOC ON)
# D16 acceptance-gate registration: whole binary under its target name
# (ctest -R "^test_edit_session$" matches verbatim, -j1, headless).
add_test(NAME test_edit_session COMMAND test_edit_session)
set_tests_properties(test_edit_session PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
  TIMEOUT 120)

# F11 Phase 2: snapping / validity / sample tools / annotation.
add_executable(test_edit_snapping
  test_edit_snapping.cpp
  ${CMAKE_SOURCE_DIR}/src/app/editing/rs_snapping_controller.cpp
)
target_link_libraries(test_edit_snapping PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
)
target_include_directories(test_edit_snapping PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_edit_snapping PROPERTIES AUTOMOC ON)
add_test(NAME test_edit_snapping COMMAND test_edit_snapping)
set_tests_properties(test_edit_snapping PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
  TIMEOUT 120)

add_executable(test_edit_validity
  test_edit_validity.cpp
  ${CMAKE_SOURCE_DIR}/src/app/editing/rs_geometry_validity.cpp
)
target_link_libraries(test_edit_validity PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
)
target_include_directories(test_edit_validity PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_edit_validity PROPERTIES AUTOMOC ON)
add_test(NAME test_edit_validity COMMAND test_edit_validity)
set_tests_properties(test_edit_validity PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
  TIMEOUT 120)

add_executable(test_edit_sample_tools
  test_edit_sample_tools.cpp
  ${CMAKE_SOURCE_DIR}/src/app/editing/rs_edit_session.cpp
  ${CMAKE_SOURCE_DIR}/src/app/editing/rs_sample_brush_tool.cpp
  ${CMAKE_SOURCE_DIR}/src/app/editing/rs_sample_erase_tool.cpp
)
target_link_libraries(test_edit_sample_tools PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  Qt6::Test
)
target_include_directories(test_edit_sample_tools PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_edit_sample_tools PROPERTIES AUTOMOC ON)
add_test(NAME test_edit_sample_tools COMMAND test_edit_sample_tools)
set_tests_properties(test_edit_sample_tools PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
  TIMEOUT 120)

add_executable(test_edit_annotation
  test_edit_annotation.cpp
  ${CMAKE_SOURCE_DIR}/src/app/editing/rs_annotation_controller.cpp
)
target_link_libraries(test_edit_annotation PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
)
target_include_directories(test_edit_annotation PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_SOURCE_DIR}/src/gui
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_edit_annotation PROPERTIES AUTOMOC ON)
add_test(NAME test_edit_annotation COMMAND test_edit_annotation)
set_tests_properties(test_edit_annotation PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
  TIMEOUT 120)

# F11 Phase 3: ROI semantics (windowed footprint/stats via qgis_analysis
# RsPixelRasterizer) + maintained spatial index.
add_executable(test_edit_roi_semantics
  test_edit_roi_semantics.cpp
  ${CMAKE_SOURCE_DIR}/src/app/editing/rs_edit_session.cpp
  ${CMAKE_SOURCE_DIR}/src/app/editing/rs_roi_semantics.cpp
  ${CMAKE_SOURCE_DIR}/src/processing/gdal/gdal_dataset_wrapper.cpp
)
target_link_libraries(test_edit_roi_semantics PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  qgis_core
  qgis_analysis
  GDAL::GDAL
)
target_include_directories(test_edit_roi_semantics PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_edit_roi_semantics PROPERTIES AUTOMOC ON)
add_test(NAME test_edit_roi_semantics COMMAND test_edit_roi_semantics)
set_tests_properties(test_edit_roi_semantics PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
  TIMEOUT 300)

add_executable(test_edit_index
  test_edit_index.cpp
  ${CMAKE_SOURCE_DIR}/src/app/editing/rs_edit_index.cpp
)
target_link_libraries(test_edit_index PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  qgis_core
)
target_include_directories(test_edit_index PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_edit_index PROPERTIES AUTOMOC ON)
add_test(NAME test_edit_index COMMAND test_edit_index)
set_tests_properties(test_edit_index PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
  TIMEOUT 600)

# F11 Phase 4: agent facts surface + persistence/interchange.
add_executable(test_edit_agent_state
  test_edit_agent_state.cpp
  ${CMAKE_SOURCE_DIR}/src/app/editing/rs_edit_session.cpp
  ${CMAKE_SOURCE_DIR}/src/app/editing/rs_edit_agent_tool.cpp
  ${CMAKE_SOURCE_DIR}/src/app/editing/rs_snapping_controller.cpp
)
target_link_libraries(test_edit_agent_state PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_agent
)
sicnu_link_jsoncpp(test_edit_agent_state)
target_include_directories(test_edit_agent_state PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_edit_agent_state PROPERTIES AUTOMOC ON)
add_test(NAME test_edit_agent_state COMMAND test_edit_agent_state)
set_tests_properties(test_edit_agent_state PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
  TIMEOUT 120)

add_executable(test_edit_persistence
  test_edit_persistence.cpp
  ${CMAKE_SOURCE_DIR}/src/app/editing/rs_edit_persistence.cpp
)
target_link_libraries(test_edit_persistence PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  qgis_core
)
target_include_directories(test_edit_persistence PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_edit_persistence PROPERTIES AUTOMOC ON)
add_test(NAME test_edit_persistence COMMAND test_edit_persistence)
set_tests_properties(test_edit_persistence PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
  TIMEOUT 300)

# F11 Phase 6: offscreen interaction E2E over the composed editing platform.
add_executable(test_editing_e2e
  test_editing_e2e.cpp
  ${CMAKE_SOURCE_DIR}/src/app/editing/rs_edit_session.cpp
  ${CMAKE_SOURCE_DIR}/src/app/editing/rs_edit_agent_tool.cpp
  ${CMAKE_SOURCE_DIR}/src/app/editing/rs_snapping_controller.cpp
  ${CMAKE_SOURCE_DIR}/src/app/editing/rs_geometry_validity.cpp
  ${CMAKE_SOURCE_DIR}/src/app/editing/rs_roi_semantics.cpp
  ${CMAKE_SOURCE_DIR}/src/app/editing/rs_sample_brush_tool.cpp
  ${CMAKE_SOURCE_DIR}/src/app/editing/rs_edit_persistence.cpp
  ${CMAKE_SOURCE_DIR}/src/processing/gdal/gdal_dataset_wrapper.cpp
)
target_link_libraries(test_editing_e2e PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  Qt6::Test
  qgis_analysis
  GDAL::GDAL
)
sicnu_link_jsoncpp(test_editing_e2e)
target_include_directories(test_editing_e2e PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_editing_e2e PROPERTIES AUTOMOC ON)
add_test(NAME test_editing_e2e COMMAND test_editing_e2e)
set_tests_properties(test_editing_e2e PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
  TIMEOUT 600)
# deployment-packaging-11 (F19): bundle manifest conformance. Drives the
# canonical verifier (scripts/verify_bundle_manifest.py) as a subprocess
# against independently synthesized golden bundles — valid /1 and /2, tamper,
# missing, extra, unsafe path, empty required prefix, ceiling breach, future
# major refusal, min_reader_schema, symlink escape — plus the PS twin lane
# when pwsh exists. Python-only on purpose: no C++ tree rebuild is needed to
# hold the packaging contract, and the verifier is a standalone artifact.
if(Python_EXECUTABLE)
  add_test(NAME bundle_manifest_conformance
    COMMAND "${Python_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/tests/fixtures/bundle_manifest/conformance.py"
            "--script" "${CMAKE_SOURCE_DIR}/scripts/verify_bundle_manifest.py")
endif()

# lab platform 12.0: classroom batch driver conformance. Python-only on
# purpose: scripts/run_classroom_batch.py is a standalone artifact, and the
# stub grader covers every isolation path (timeout kill, signal crash,
# corrupt artifact, usage errors, submission cap, concurrency cap,
# Unicode/space paths, byte-identical reruns). When the real CLI target
# exists one corrupt-artifact lane additionally runs through the genuine
# grading seam (typed exit-3 row).
if(Python_EXECUTABLE)
  if(TARGET sicnu_geo_rs_cli)
    add_test(NAME classroom_batch_conformance
      COMMAND "${Python_EXECUTABLE}"
              "${CMAKE_SOURCE_DIR}/tests/fixtures/classroom_batch/conformance.py"
              "--real-cli" "$<TARGET_FILE:sicnu_geo_rs_cli>")
  else()
    add_test(NAME classroom_batch_conformance
      COMMAND "${Python_EXECUTABLE}"
              "${CMAKE_SOURCE_DIR}/tests/fixtures/classroom_batch/conformance.py")
  endif()
  set_tests_properties(classroom_batch_conformance PROPERTIES TIMEOUT 300)
endif()

# lab platform 12.0: launcher parity fixtures. Executes the POSIX launchers
# and locks the cross-platform contract (argument spellings, exit-code
# propagation, offline env wiring). The real foundry binary lane runs when
# the target exists; the Windows .cmd twins hold the same contract by
# construction and are reviewed statically (no Windows host here).
if(Python_EXECUTABLE)
  if(TARGET sicnu_generate_samples)
    add_test(NAME launcher_parity_conformance
      COMMAND "${Python_EXECUTABLE}"
              "${CMAKE_SOURCE_DIR}/tests/fixtures/launcher_parity/conformance.py"
              "--gen-bin" "$<TARGET_FILE:sicnu_generate_samples>"
              "--repo" "${CMAKE_SOURCE_DIR}")
  else()
    add_test(NAME launcher_parity_conformance
      COMMAND "${Python_EXECUTABLE}"
              "${CMAKE_SOURCE_DIR}/tests/fixtures/launcher_parity/conformance.py"
              "--repo" "${CMAKE_SOURCE_DIR}")
  endif()
  set_tests_properties(launcher_parity_conformance PROPERTIES TIMEOUT 600)
endif()

# Deployment 11.0 (F19): environment self-check suite. Qt-free (geospatial
# doctor layer + offline gate) — the io-test link set, not the fat one.
sicnu_add_io_test(test_env_doctor)
target_compile_definitions(test_env_doctor PRIVATE
  CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
# ds41-build-portability: configure-time feature-probe contract (compile-check
# results in ${CMAKE_BINARY_DIR}/sicnu_feature_probes.h, every macro defined
# 0/1, GDAL quieting / std::format cross-checked at runtime). Does not compile
# without cmake/SicnuFeatureProbes.cmake — that is the intended potency.
sicnu_add_io_test(test_feature_probes)
# Verification Platform 11.0 (F09 determinism truth): cross-family replay
# corpus — identical input twice must be byte-identical; corpus claims must
# be triple-published (class scan / schema stamp / sidecar), never
# self-certified (append-only block).
sicnu_add_test(test_contract_determinism_11)
target_link_libraries(test_contract_determinism_11 PRIVATE sicnu_contracts)
sicnu_link_jsoncpp(test_contract_determinism_11)

# Verification Platform 11.0 (F09 packages C/D/E): metamorphic oracle
# relations with sensitivity controls, independent numeric references,
# oracle mutation-kill discrimination, and the failure/cancel/atomic lane
# (append-only block).
sicnu_add_test(test_verification_metamorphic_11)
target_link_libraries(test_verification_metamorphic_11 PRIVATE sicnu_contracts)
sicnu_link_jsoncpp(test_verification_metamorphic_11)

sicnu_add_test(test_verification_numeric_reference_11)
target_link_libraries(test_verification_numeric_reference_11 PRIVATE sicnu_contracts)
sicnu_link_jsoncpp(test_verification_numeric_reference_11)

sicnu_add_test(test_mutation_kill_11)
target_link_libraries(test_mutation_kill_11 PRIVATE sicnu_contracts)
sicnu_link_jsoncpp(test_mutation_kill_11)

sicnu_add_test(test_verification_failure_11)
target_link_libraries(test_verification_failure_11 PRIVATE sicnu_contracts)
sicnu_link_jsoncpp(test_verification_failure_11)

sicnu_add_test(test_contract_cross_surface_11)
target_link_libraries(test_contract_cross_surface_11 PRIVATE sicnu_contracts)
sicnu_link_jsoncpp(test_contract_cross_surface_11)


# ── Platform 12.0 · Scientific Verification 12 ────────────────────────────
# Append-only block (track glm53-scientific-verification-12). Do not reorder
# the blocks above; add new lanes at the end.
#
# These lanes close the loop on ERROR LISTS the platform already produces but
# never asserted were empty. The defects they gate were all live on master:
#   E-5   data/agent/capabilities/preprocess.json unparseable since #1024;
#         CapabilityKnowledge::loadProblems() had zero consumers.
#   E-8a  data/help/diagnostics.json duplicated two ids, one with an invalid
#         retry enum that caused the whole page to be dropped.
#   E-8b  the `env` diagnostic family is declared by env_doctor.h and emitted
#         by env_doctor.cpp but absent from DiagnosticFamily (out-of-scope fix;
#         the gate still reports it).
#   E-0   an unprovisioned PROJ_DATA made 5 healthy suites report RED with
#         product-defect-shaped exceptions.
sicnu_add_test(test_platform_integrity_12)
target_link_libraries(test_platform_integrity_12 PRIVATE sicnu_contracts)
sicnu_link_jsoncpp(test_platform_integrity_12)

sicnu_add_test(test_verification_env_12)
target_link_libraries(test_verification_env_12 PRIVATE sicnu_contracts)
sicnu_link_jsoncpp(test_verification_env_12)

# O-11 (help/diagnostic corpus integrity). The lane touches only Sicnu::help
# + jsoncpp + Qt Core: it reads data/help/** from the source tree through
# HelpContentStore and asserts on the load result. It used to ride the heavy
# sicnu_add_test link set (qgis_gui / sicnu_operators / sicnu_agent /
# sicnu_workflow_runtime), which is pure build cost for a corpus gate and
# made it one of the slowest executables in the suite. Same assertions,
# same tags — only the link inputs changed.
sicnu_add_help_test(test_help_integrity_12)
target_compile_definitions(test_help_integrity_12 PRIVATE
  CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
sicnu_link_jsoncpp(test_help_integrity_12)

# E-13: a byte comparison cannot tell "content moved" from "the graph no longer
# holds together". This lane asserts the STRUCTURAL invariant (no dangling edge
# endpoint) on both the committed snapshot and a fresh live assembly, so a
# regeneration can never silently bless broken references into the baseline.
sicnu_add_test(test_snapshot_drift_12)
target_link_libraries(test_snapshot_drift_12 PRIVATE sicnu_contracts)
sicnu_link_jsoncpp(test_snapshot_drift_12)

# E-16: `origin/master` ships 25 unparseable `data/**/*.json` files, landed by
# two EVIL MERGES (43dcf19cd truncated-capability-block x19, 4713528ef
# zero-byte x4). E-15 was the same defect in one file; E-16 is the census.
# Existing gates each miss it for a structural reason -- one inspects a
# different directory (test_capability_drift.cpp:63), one reads a failure list
# nothing asserts empty, one compares a snapshot against itself. This lane
# asserts the property every consumer assumes: every shipped data file parses.
sicnu_add_test(test_data_parse_integrity_12)
target_link_libraries(test_data_parse_integrity_12 PRIVATE sicnu_contracts)
sicnu_link_jsoncpp(test_data_parse_integrity_12)

# =============================================================================
# Track ds41-fuzz-boundaries — boundary fuzz/property lanes (append-only).
#
# Follows the 7.0/8.0 "bounded contract fuzz" convention: deterministic seeds,
# hard input caps (≤512 B, ≤300 iterations/seed), CI-safe total-function
# oracles. Mutators, the ddmin minimizer and the optional corpus recorder live
# in support/fuzz_corpus.h (this track adds them; support/bounded_fuzz.h stays
# untouched because it is shared with other tracks).
#
#   test_contract_fuzz_frame   — exprs::IpcFrame framing + exprs::Ipc envelope
#                                (known-answer legs: test_exprs_ipc.cpp)
#   test_contract_fuzz_payload — workflow schema, plugin UI schema + ui events,
#                                manifest ports, diagnostics, dependency version
#                                ranges, plugin index pins
#                                (known-answer legs: test_exprs_workflow_schema,
#                                test_plugin_ui_schema, test_exprs_plugin_system)
#   test_contract_fuzz_paths   — exprs::PathPolicy lexical/containment cage,
#                                asserted against an INDEPENDENT resolution
#
# All three link only Catch2 + sicnu_sdk (jsoncpp-only static library) — no Qt,
# no QGIS, no GDAL — so they build in seconds and run in well under a minute.
# The path lane creates its fixtures under std::filesystem::temp_directory_path.
sicnu_add_sdk_test(test_contract_fuzz_frame)
sicnu_link_jsoncpp(test_contract_fuzz_frame)
target_include_directories(test_contract_fuzz_frame PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

sicnu_add_sdk_test(test_contract_fuzz_payload)
sicnu_link_jsoncpp(test_contract_fuzz_payload)
target_include_directories(test_contract_fuzz_payload PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

sicnu_add_sdk_test(test_contract_fuzz_paths)
sicnu_link_jsoncpp(test_contract_fuzz_paths)
target_include_directories(test_contract_fuzz_paths PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# ds41-performance-observatory: cross-module performance & memory observatory
# (append-only block). Two targets sharing tests/perf/perf_observatory.h:
#   * test_perf_io_observatory — Qt-free raster I/O + atomic-writer workloads
#     (links only Sicnu::Geospatial, matching the io test family).
#   * test_perf_observatory    — dataset catalog scaling, governance-store
#     paging, TaskCenter dispatch/DAG, temporal tile streaming, tiled
#     inference (execution-plane link set plus the workspace-browser panel
#     translation unit for the paging model).
# SICNU_OBS_SCALE=small|mid|scale selects the fixture ladder; ctest runs small.
# Records land in SICNU_OBS_OUT or a private temp directory, never the repo.
# All gates are structural (counts, complexity exponents, memory bounds,
# semantic equivalence) — no absolute millisecond budgets, so the suite stays
# low-flake on shared CI.
sicnu_add_io_test(test_perf_io_observatory)
target_include_directories(test_perf_io_observatory PRIVATE ${CMAKE_SOURCE_DIR}/tests)

sicnu_add_test(test_perf_observatory TIMEOUT 900 RUN_SERIAL TRUE)
target_sources(test_perf_observatory PRIVATE
  ${CMAKE_SOURCE_DIR}/src/app/panels/workspace_browser_panel.cpp
)
target_include_directories(test_perf_observatory PRIVATE
  ${CMAKE_SOURCE_DIR}/tests
)
sicnu_link_jsoncpp(test_perf_observatory)
if(TARGET Sicnu::session_map)
  target_link_libraries(test_perf_observatory PRIVATE Sicnu::session_map)
endif()

# Link-set note, recorded because it was measured rather than assumed: the
# observatory uses no agent API, but sicnu_data / sicnu_task_center /
# sicnu_operators all PUBLIC-reference sicnu_agent, and sicnu_agent links
# qgis_gui. Dropping qgis_gui from THIS target's own link list therefore does
# not remove it from the link (verified in build-obs/build.ninja), so the
# standard sicnu_add_test registration is kept and the vendored gui is built
# anyway. Anyone who wants to make the observatory gui-free has to break that
# transitive edge in production CMake, not here.

# ---------------------------------------------------------------------------
# Data Scale 13.0 (Track C) — structural scale oracles for the DataManager
# catalog (O(1) snapshot publication, generation-scoped path index) and the
# GovernanceStore keyset pagination. Gates are structural counts and
# equivalence checks against a verbatim copy of the pre-change lookup
# algorithm; no absolute millisecond budgets. The 100k rungs are hermetic
# (in-memory provider) and complete in seconds, so they run in the standard
# lane; RUN_SERIAL keeps the shared build host quiet during the ladder.
# ---------------------------------------------------------------------------
qt_add_executable(test_data_scale
  test_data_scale.cpp
)
target_link_libraries(test_data_scale PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  sicnu_data
  # The governance plan oracle asserts SQLite's own query plan for the paged
  # SELECT, so the suite needs the SQLite C API directly (sicnu_data links it
  # PRIVATE, which does not propagate to consumers).
  SQLite::SQLite3
)
target_include_directories(test_data_scale PRIVATE
  ${CMAKE_SOURCE_DIR}/src
)
sicnu_discover_tests(test_data_scale TIMEOUT 600 RUN_SERIAL TRUE)

# Platform 13.0: EO Model Runtime 13.0 ensemble execution (this track).
# All three suites share the model-runtime closure; appended at the tail so
# parallel tracks' append-only hunks stay conflict-free.
# ---------------------------------------------------------------------------
sicnu_add_test(test_ensemble_detection TIMEOUT 300)
target_link_libraries(test_ensemble_detection PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    qgis_core
    sicnu_processing
    sicnu_task_center
    sicnu_operators
    ${OpenCV_LIBS}
)
sicnu_link_jsoncpp(test_ensemble_detection)
target_include_directories(test_ensemble_detection PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/tests
    ${CMAKE_BINARY_DIR}
)

sicnu_add_test(test_ensemble_scene TIMEOUT 300)
target_link_libraries(test_ensemble_scene PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    qgis_core
    sicnu_processing
    sicnu_task_center
    sicnu_operators
    ${OpenCV_LIBS}
)
sicnu_link_jsoncpp(test_ensemble_scene)
target_include_directories(test_ensemble_scene PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/tests
    ${CMAKE_BINARY_DIR}
)

sicnu_add_test(test_ensemble_parallel TIMEOUT 600 RUN_SERIAL TRUE)
target_link_libraries(test_ensemble_parallel PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    qgis_core
    sicnu_processing
    sicnu_task_center
    sicnu_operators
    ${OpenCV_LIBS}
)
sicnu_link_jsoncpp(test_ensemble_parallel)
target_include_directories(test_ensemble_parallel PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/tests
    ${CMAKE_BINARY_DIR}
)

# Mission Runtime 13.0 — single-authority persistence, real mission:* tools,
# surface parity and scale gates.
#
# These targets compile the REAL mission domain sources (the same files the
# desktop shell and sicnu_agent compile) so the gates exercise production
# code. The Qt-Core-only task-space lanes above (test_mission_stage,
# test_mission_surface_parity, test_mission_scale_benchmark) stay untouched;
# these four add the persistence chain (which reaches QGIS through
# object_identity / selection_context, exactly like test_mission_context) and
# the agent tool objects (spatial_tool_registry + contracts + mission_tools,
# which are QGIS-free by construction).
# ---------------------------------------------------------------------------
# #1186: shared with mission-runtime-gate (cmake/SicnuMissionRuntimeSources.cmake)
set(SICNU_MISSION_SRC_ROOT "${CMAKE_SOURCE_DIR}")
include("${CMAKE_SOURCE_DIR}/cmake/SicnuMissionRuntimeSources.cmake")
set(SICNU_MISSION_RUNTIME_SOURCES ${SICNU_MISSION_RUNTIME_SOURCES_FULL})

function(sicnu_add_mission_runtime_test NAME)
  # The mission-runtime test sources live in mission-runtime-gate/tests/ (not
  # tests/): a bare ${NAME}.cpp fails to configure. Pre-existing master
  # breakage (mission-runtime 13.0, PR #1139); repaired minimally here because
  # this branch's union tree must configure.
  add_executable(${NAME} ${CMAKE_SOURCE_DIR}/mission-runtime-gate/tests/${NAME}.cpp
                 ${SICNU_MISSION_RUNTIME_SOURCES})
  target_link_libraries(${NAME} PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    Qt6::Xml
    Qt6::Gui
    Qt6::Widgets
    qgis_core
    qgis_gui
    sicnu_data
  )
  target_include_directories(${NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
  )
  # The parity gate parses the real MCP dispatch / provider / Pi bridge /
  # CMake registrations from the source tree, so a forgotten surface wiring
  # fails the test.
  target_compile_definitions(${NAME} PRIVATE
    SICNU_MISSION_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
  )
  sicnu_link_jsoncpp(${NAME})
  set_target_properties(${NAME} PROPERTIES AUTOMOC ON)
  sicnu_discover_tests(${NAME})
endfunction()

sicnu_add_mission_runtime_test(test_mission_runtime_persistence)
sicnu_add_mission_runtime_test(test_mission_tools)
sicnu_add_mission_runtime_test(test_mission_runtime_parity)
sicnu_add_mission_runtime_test(test_mission_runtime_scale)

# Spectral Intelligence 13.0 work package A: TCIMF / OSP detector kernels.
add_executable(test_spectral_tcimf test_spectral_tcimf.cpp)
target_link_libraries(test_spectral_tcimf PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  sicnu_processing
  GDAL::GDAL
)
sicnu_link_jsoncpp(test_spectral_tcimf)
target_include_directories(test_spectral_tcimf PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_spectral_tcimf)

add_executable(test_spectral_osp test_spectral_osp.cpp)
target_link_libraries(test_spectral_osp PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  sicnu_processing
  GDAL::GDAL
)
sicnu_link_jsoncpp(test_spectral_osp)
target_include_directories(test_spectral_osp PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_spectral_osp)

# Spectral Intelligence 13.0 work package A: TCIMF / OSP operator contracts.
add_executable(test_spectral_detection_13 test_spectral_detection_13.cpp)
target_link_libraries(test_spectral_detection_13 PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  sicnu_processing
  sicnu_operators
  GDAL::GDAL
)
sicnu_link_jsoncpp(test_spectral_detection_13)
target_include_directories(test_spectral_detection_13 PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
  ${CMAKE_SOURCE_DIR}/tests
)
sicnu_discover_tests(test_spectral_detection_13)

# Spectral Intelligence 13.0 work package B: independent background raster.
add_executable(test_spectral_background_raster test_spectral_background_raster.cpp)
target_link_libraries(test_spectral_background_raster PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  sicnu_processing
  sicnu_operators
  GDAL::GDAL
)
sicnu_link_jsoncpp(test_spectral_background_raster)
target_include_directories(test_spectral_background_raster PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
  ${CMAKE_SOURCE_DIR}/tests
)
sicnu_discover_tests(test_spectral_background_raster)

# RS14 suitability assessor tests (pure science-core leaf; no qgis/gdal).
qt_add_executable(test_suitability_core
  test_suitability_core.cpp
)
target_link_libraries(test_suitability_core PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  sicnu_suitability
)
target_include_directories(test_suitability_core PRIVATE
  ${CMAKE_SOURCE_DIR}/src
)
sicnu_discover_tests(test_suitability_core)

qt_add_executable(test_suitability_spatial
  test_suitability_spatial.cpp
)
target_link_libraries(test_suitability_spatial PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  sicnu_suitability
)
target_include_directories(test_suitability_spatial PRIVATE
  ${CMAKE_SOURCE_DIR}/src
)
sicnu_discover_tests(test_suitability_spatial)

qt_add_executable(test_suitability_spectral
  test_suitability_spectral.cpp
)
target_link_libraries(test_suitability_spectral PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  sicnu_suitability
)
target_include_directories(test_suitability_spectral PRIVATE
  ${CMAKE_SOURCE_DIR}/src
)
sicnu_discover_tests(test_suitability_spectral)

qt_add_executable(test_suitability_temporal
  test_suitability_temporal.cpp
)
target_link_libraries(test_suitability_temporal PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  sicnu_suitability
)
target_include_directories(test_suitability_temporal PRIVATE
  ${CMAKE_SOURCE_DIR}/src
)
sicnu_discover_tests(test_suitability_temporal)

qt_add_executable(test_suitability_labels
  test_suitability_labels.cpp
)
target_link_libraries(test_suitability_labels PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  sicnu_suitability
)
target_include_directories(test_suitability_labels PRIVATE
  ${CMAKE_SOURCE_DIR}/src
)
sicnu_discover_tests(test_suitability_labels)

qt_add_executable(test_suitability_store_provider
  test_suitability_store_provider.cpp
)
target_link_libraries(test_suitability_store_provider PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  sicnu_suitability
  Sicnu::dataset
  SQLite::SQLite3
)
target_include_directories(test_suitability_store_provider PRIVATE
  ${CMAKE_SOURCE_DIR}/src
)
sicnu_discover_tests(test_suitability_store_provider)

qt_add_executable(test_suitability_profiles
  test_suitability_profiles.cpp
)
target_link_libraries(test_suitability_profiles PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  sicnu_suitability
)
target_include_directories(test_suitability_profiles PRIVATE
  ${CMAKE_SOURCE_DIR}/src
)
sicnu_discover_tests(test_suitability_profiles)

qt_add_executable(test_suitability_adversarial
  test_suitability_adversarial.cpp
)
target_link_libraries(test_suitability_adversarial PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  sicnu_suitability
  Sicnu::dataset
  SQLite::SQLite3
)
target_include_directories(test_suitability_adversarial PRIVATE
  ${CMAKE_SOURCE_DIR}/src
)
sicnu_discover_tests(test_suitability_adversarial)

qt_add_executable(test_suitability_uncertainty
  test_suitability_uncertainty.cpp
)
target_link_libraries(test_suitability_uncertainty PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  sicnu_suitability
)
target_include_directories(test_suitability_uncertainty PRIVATE
  ${CMAKE_SOURCE_DIR}/src
)
sicnu_discover_tests(test_suitability_uncertainty)

qt_add_executable(test_suitability_teaching
  test_suitability_teaching.cpp
)
target_link_libraries(test_suitability_teaching PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  sicnu_suitability
)
target_include_directories(test_suitability_teaching PRIVATE
  ${CMAKE_SOURCE_DIR}/src
)
sicnu_discover_tests(test_suitability_teaching)

# RS14 Slice H3: the suitability agent adapter lives INSIDE sicnu_suitability
# precisely so this suite needs no qgis/agent link; the agent-side dispatch
# shell in data_platform_tools.cpp forwards here (surface parity is covered by
# the existing test_surface_parity suite; capability-drift files are
# deliberately untouched — #1151 avoidance).
qt_add_executable(test_suitability_agent_tools
  test_suitability_agent_tools.cpp
)
target_link_libraries(test_suitability_agent_tools PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  sicnu_suitability
)
target_include_directories(test_suitability_agent_tools PRIVATE
  ${CMAKE_SOURCE_DIR}/src
)
sicnu_discover_tests(test_suitability_agent_tools)
# =============================================================================
# RS14-01 Scientific Data Passport: unified scientific asset state layer.
# Core contracts run in the jsoncpp-only lane (no Qt, no QGIS, no GDAL);
# the GDAL facts collector gets its own io-lane target once it lands.
# =============================================================================
sicnu_add_sdk_test(test_scientific_state_core)
target_link_libraries(test_scientific_state_core PRIVATE Sicnu::ScientificState)
sicnu_add_sdk_test(test_scientific_state_resolver)
target_link_libraries(test_scientific_state_resolver PRIVATE Sicnu::ScientificState)
sicnu_add_sdk_test(test_scientific_state_geo)
target_link_libraries(test_scientific_state_geo PRIVATE Sicnu::ScientificState)
sicnu_add_sdk_test(test_scientific_state_provenance)
target_link_libraries(test_scientific_state_provenance PRIVATE Sicnu::ScientificState)
sicnu_add_sdk_test(test_scientific_state_diff)
target_link_libraries(test_scientific_state_diff PRIVATE Sicnu::ScientificState)

sicnu_add_sdk_test(test_scientific_state_teaching)
target_link_libraries(test_scientific_state_teaching PRIVATE Sicnu::ScientificState)

sicnu_add_io_test(test_scientific_state_gdal)
target_link_libraries(test_scientific_state_gdal PRIVATE Sicnu::ScientificStateGdal)

# RS14-01 Slice F4: catalog adapter — Qt lane (real DataManager + GDAL).
sicnu_add_test(test_scientific_state_catalog)
target_link_libraries(test_scientific_state_catalog PRIVATE
  Sicnu::ScientificState
  Sicnu::ScientificStateCatalog
  Sicnu::ScientificStateGdal
)
set_target_properties(test_scientific_state_catalog PROPERTIES AUTOMOC ON)
sicnu_add_sdk_test(test_scientific_state_fixtures)
target_link_libraries(test_scientific_state_fixtures PRIVATE Sicnu::ScientificState)
sicnu_add_sdk_test(test_scientific_state_review)
target_link_libraries(test_scientific_state_review PRIVATE Sicnu::ScientificState)

# Science Context Broker & Skill Router (Qt-free; Sicnu::ScienceContext).
sicnu_add_sdk_test(test_science_context_broker)
target_link_libraries(test_science_context_broker PRIVATE Sicnu::ScienceContext)

# Science context live-authority probes: broker + real ScientificRecipeRegistry
# (in-repo pack via CMAKE_SOURCE_DIR) + real GDAL facts collector (io lane).
add_executable(test_science_context_live_authorities test_science_context_live_authorities.cpp)
target_link_libraries(test_science_context_live_authorities PRIVATE
  Catch2::Catch2WithMain
  Sicnu::ScienceContext
  sicnu_recipes
  Sicnu::ScientificStateGdal
)
target_include_directories(test_science_context_live_authorities PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(test_science_context_live_authorities PRIVATE
  CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
set_target_properties(test_science_context_live_authorities PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
sicnu_discover_tests(test_science_context_live_authorities)
# RS14 agent-benchmark harness: pure-C++ trajectory benchmark lane
# (sicnu_agentbench; no Qt, no GDAL, no engine — see src/agentbench/CMakeLists.txt).
add_executable(test_agentbench_core test_agentbench_core.cpp)
target_link_libraries(test_agentbench_core PRIVATE
  Catch2::Catch2WithMain
  sicnu_agentbench
)
sicnu_link_jsoncpp(test_agentbench_core)
target_include_directories(test_agentbench_core PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_agentbench_core PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
sicnu_discover_tests(test_agentbench_core TEST_PREFIX "test_agentbench_core::")

# RS14 agent-benchmark Slice A: case schema contract.
add_executable(test_agentbench_case test_agentbench_case.cpp)
target_link_libraries(test_agentbench_case PRIVATE
  Catch2::Catch2WithMain
  sicnu_agentbench
)
sicnu_link_jsoncpp(test_agentbench_case)
target_include_directories(test_agentbench_case PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_agentbench_case PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
sicnu_discover_tests(test_agentbench_case TEST_PREFIX "test_agentbench_case::")

# RS14 agent-benchmark Slice B: trace schema + replay validation.
add_executable(test_agentbench_trace test_agentbench_trace.cpp)
target_link_libraries(test_agentbench_trace PRIVATE
  Catch2::Catch2WithMain
  sicnu_agentbench
)
sicnu_link_jsoncpp(test_agentbench_trace)
target_include_directories(test_agentbench_trace PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_agentbench_trace PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
sicnu_discover_tests(test_agentbench_trace TEST_PREFIX "test_agentbench_trace::")

# RS14 agent-benchmark Slice C: deterministic fake agent.
add_executable(test_agentbench_fake test_agentbench_fake.cpp)
target_link_libraries(test_agentbench_fake PRIVATE
  Catch2::Catch2WithMain
  sicnu_agentbench
)
sicnu_link_jsoncpp(test_agentbench_fake)
target_include_directories(test_agentbench_fake PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_agentbench_fake PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
sicnu_discover_tests(test_agentbench_fake TEST_PREFIX "test_agentbench_fake::")

# RS14 agent-benchmark Slice D: hidden-invariant oracle.
add_executable(test_agentbench_invariants test_agentbench_invariants.cpp)
target_link_libraries(test_agentbench_invariants PRIVATE
  Catch2::Catch2WithMain
  sicnu_agentbench
)
sicnu_link_jsoncpp(test_agentbench_invariants)
target_include_directories(test_agentbench_invariants PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_agentbench_invariants PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
sicnu_discover_tests(test_agentbench_invariants TEST_PREFIX "test_agentbench_invariants::")

# RS14 agent-benchmark Slice E: 8-metric evaluator + failure taxonomy.
add_executable(test_agentbench_evaluator test_agentbench_evaluator.cpp)
target_link_libraries(test_agentbench_evaluator PRIVATE
  Catch2::Catch2WithMain
  sicnu_agentbench
)
sicnu_link_jsoncpp(test_agentbench_evaluator)
target_include_directories(test_agentbench_evaluator PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_agentbench_evaluator PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
sicnu_discover_tests(test_agentbench_evaluator TEST_PREFIX "test_agentbench_evaluator::")

# RS14 agent-benchmark Slice G: JSON + Markdown reports.
add_executable(test_agentbench_report test_agentbench_report.cpp)
target_link_libraries(test_agentbench_report PRIVATE
  Catch2::Catch2WithMain
  sicnu_agentbench
)
sicnu_link_jsoncpp(test_agentbench_report)
target_include_directories(test_agentbench_report PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_agentbench_report PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
sicnu_discover_tests(test_agentbench_report TEST_PREFIX "test_agentbench_report::")

# RS14 agent-benchmark Slice H: suite runner.
add_executable(test_agentbench_suite test_agentbench_suite.cpp)
target_link_libraries(test_agentbench_suite PRIVATE
  Catch2::Catch2WithMain
  sicnu_agentbench
)
sicnu_link_jsoncpp(test_agentbench_suite)
target_include_directories(test_agentbench_suite PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_agentbench_suite PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
sicnu_discover_tests(test_agentbench_suite TEST_PREFIX "test_agentbench_suite::")

# RS14 agent-benchmark Slice H: corpus validation (reads data/agent/bench).
add_executable(test_agentbench_corpus test_agentbench_corpus.cpp)
target_link_libraries(test_agentbench_corpus PRIVATE
  Catch2::Catch2WithMain
  sicnu_agentbench
)
sicnu_link_jsoncpp(test_agentbench_corpus)
target_include_directories(test_agentbench_corpus PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
target_compile_definitions(test_agentbench_corpus PRIVATE
  CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
set_target_properties(test_agentbench_corpus PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
sicnu_discover_tests(test_agentbench_corpus TEST_PREFIX "test_agentbench_corpus::")

# Provider error-path regression lane (processing-provider-atomic-errors
# track): fail-closed contracts for external tool wrappers, short writes and
# flush failures. Append-only per the shared-tests-file convention.
add_executable(test_processing_error_paths test_processing_error_paths.cpp)
target_link_libraries(test_processing_error_paths PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
  GDAL::GDAL
)
target_include_directories(test_processing_error_paths PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_processing_error_paths)

# Evidence-semantics seam (hardening/verifier-grader-explain-evidence):
# composes the verifier and grader leaves ABOVE themselves — a real
# sicnu_verifier report projected into a grader verifier_verdict evidence
# item. The leaf libraries stay unlinked from each other (layer guards in
# src/grader + src/explain); this is the only place they meet. Test-lane
# registration for the three schema files arrives via PR #1246.
add_executable(test_evidence_seam test_evidence_seam.cpp)
target_link_libraries(test_evidence_seam PRIVATE
  Catch2::Catch2WithMain
  sicnu_grader
  sicnu_verifier
)
sicnu_link_jsoncpp(test_evidence_seam)
target_include_directories(test_evidence_seam PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_evidence_seam TEST_PREFIX "test_evidence_seam::")

# ── Undergraduate Lab Cockpit projections (Catch2 + Sicnu::teaching) ──────
# Qt-free leaf suite: course/timeline/readiness/autonomy/feedback/session.
function(sicnu_add_teaching_test NAME)
  add_executable(${NAME} ${NAME}.cpp)
  target_link_libraries(${NAME} PRIVATE
    Catch2::Catch2WithMain
    Sicnu::teaching
  )
  target_include_directories(${NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
  )
  target_compile_definitions(${NAME} PRIVATE
    SICNU_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
  )
  set_target_properties(${NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  sicnu_discover_tests(${NAME})
endfunction()

sicnu_add_teaching_test(test_teaching_lab_cockpit)

# Undergraduate Lab Cockpit offscreen widget smoke (completion
# teaching-cockpit-live-integration): drives the REAL dock widgets over the
# REAL authorities — runtime registry probe, OutputVerifier grading engine
# (real GTiff + shipped rules), ExperimentStore + CapsuleBuilder + CapsuleIO
# export, and the session file across a restart. Compiles the dock TUs;
# heavy deps (qgis/processing/operators) arrive transitively via sicnu_agent.
add_executable(test_teaching_cockpit_smoke
  test_teaching_cockpit_smoke.cpp
  ${CMAKE_SOURCE_DIR}/src/app/teaching/course_home_page.cpp
  ${CMAKE_SOURCE_DIR}/src/app/teaching/guided_lab_workspace.cpp
  ${CMAKE_SOURCE_DIR}/src/app/teaching/lab_cockpit_dock.cpp
  ${CMAKE_SOURCE_DIR}/src/app/teaching/lab_validate_service.cpp
)
target_link_libraries(test_teaching_cockpit_smoke PRIVATE
  Catch2::Catch2WithMain
  Sicnu::teaching
  sicnu_agent
  Sicnu::experiment
  Sicnu::dataset
  Sicnu::experiment_capsule
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  sicnu_sample_foundry
)
target_include_directories(test_teaching_cockpit_smoke PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_SOURCE_DIR}/src/app/teaching
  ${CMAKE_SOURCE_DIR}/tests
  ${CMAKE_SOURCE_DIR}/tools
  ${CMAKE_BINARY_DIR}
)
target_compile_definitions(test_teaching_cockpit_smoke PRIVATE
  SICNU_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
  CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
set_target_properties(test_teaching_cockpit_smoke PROPERTIES
  AUTOMOC ON
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
)
set(_smoke_dl_paths "")
if(SICNU_TEST_DL_PATHS)
  set(_smoke_dl_paths DL_PATHS ${SICNU_TEST_DL_PATHS})
endif()
catch_discover_tests(test_teaching_cockpit_smoke
  DISCOVERY_MODE PRE_TEST
  TEST_PREFIX "teaching_cockpit_smoke: "
  ${_smoke_dl_paths}
  PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
# ── Teacher Authoring & Assessment Console (Catch2 + Sicnu::teaching_admin) ──
function(sicnu_add_teaching_admin_test NAME)
  add_executable(${NAME} ${NAME}.cpp)
  target_link_libraries(${NAME} PRIVATE
    Catch2::Catch2WithMain
    Sicnu::teaching_admin
    Qt6::Core
  )
  target_include_directories(${NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
  )
  set_target_properties(${NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
  )
  sicnu_discover_tests(${NAME} TEST_PREFIX "${NAME}::")
endfunction()
# Fake grading CLI (plain C++, no Qt): same exit/transcript contract as
# `sicnu_geo_rs_cli lab --grade` — lets test_teaching_admin_core drive the
# real-shaped grader path hermetically (see helper file header).
add_executable(test_teaching_fake_grader_cli helper_teaching_fake_grader_cli.cpp)
set_target_properties(test_teaching_fake_grader_cli PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
)
sicnu_add_teaching_admin_test(test_teaching_admin_core)
target_compile_definitions(test_teaching_admin_core PRIVATE
  FAKE_GRADER_CLI_DIR="${CMAKE_BINARY_DIR}"
)
# The core suite spawns the fake grader CLI by path: make a target-scoped
# build (no `all`) produce it too.
add_dependencies(test_teaching_admin_core test_teaching_fake_grader_cli)

# Offscreen widget smoke for the Teacher Authoring dock: compiles the dock TU
# into the test (no QGIS, no app binary) and drives one real batch-grade
# round trip through the fake grader CLI.
add_executable(test_teaching_admin_dock_smoke
  test_teaching_admin_dock_smoke.cpp
  ${CMAKE_SOURCE_DIR}/src/app/teaching_admin/teaching_admin_dock.cpp
  ${CMAKE_SOURCE_DIR}/src/app/teaching_admin/teaching_admin_dock.h
)
target_link_libraries(test_teaching_admin_dock_smoke PRIVATE
  Catch2::Catch2WithMain
  Sicnu::teaching_admin
  Qt6::Widgets
)
target_include_directories(test_teaching_admin_dock_smoke PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_SOURCE_DIR}/src/app/teaching_admin
)
target_compile_definitions(test_teaching_admin_dock_smoke PRIVATE
  FAKE_GRADER_CLI_DIR="${CMAKE_BINARY_DIR}"
)
add_dependencies(test_teaching_admin_dock_smoke test_teaching_fake_grader_cli)
set_target_properties(test_teaching_admin_dock_smoke PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
)
sicnu_discover_tests(test_teaching_admin_dock_smoke TEST_PREFIX "test_teaching_admin_dock_smoke::")

# Teaching foundation E2E (hardening/curriculum-lab-recipes-foundry): foundry
# → lab documents → v3 runtime/checkpoint → recipe compile → pack/grading
# refs, over the existing authorities only. Qt-free; GDAL arrives through
# sicnu_sample_foundry, so discovery carries the same DL_PATHS / LSAN
# treatment as test_sample_fixtures (#730, sanitizer lanes).
add_executable(test_teaching_foundation_e2e test_teaching_foundation_e2e.cpp)
target_link_libraries(test_teaching_foundation_e2e PRIVATE
  Catch2::Catch2WithMain
  sicnu_lab_runtime
  sicnu_recipes
  sicnu_sample_foundry
)
target_include_directories(test_teaching_foundation_e2e PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/tools
  ${CMAKE_BINARY_DIR}
)
target_compile_definitions(test_teaching_foundation_e2e PRIVATE
  CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
set_target_properties(test_teaching_foundation_e2e PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
)
set(_e2e_dl_paths "")
if(SICNU_TEST_DL_PATHS)
  set(_e2e_dl_paths DL_PATHS ${SICNU_TEST_DL_PATHS})
endif()
set(_e2e_discovery_props "")
if(ENABLE_SANITIZERS)
  list(APPEND _e2e_discovery_props "ENVIRONMENT_MODIFICATION" "LSAN_OPTIONS=set:detect_leaks=0")
endif()
catch_discover_tests(test_teaching_foundation_e2e
  DISCOVERY_MODE PRE_TEST
  TEST_PREFIX "teaching_foundation_e2e: "
  ${_e2e_dl_paths}
  PROPERTIES ${_e2e_discovery_props}
)

# ── Workbench/Project lifecycle shell track (completion/workbench-project-lifecycle-shell-fixtures) ──
# The openProject transaction (probe → clear → story boundary → store → read
# + failure rollback), testable headless via ProjectContext::createHeadless.
add_executable(test_project_session_boundary
  test_project_session_boundary.cpp
)
target_link_libraries(test_project_session_boundary PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Sicnu::qgis_display
  qgis_core
)
target_include_directories(test_project_session_boundary PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
sicnu_link_jsoncpp(test_project_session_boundary)
set_target_properties(test_project_session_boundary PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_project_session_boundary)

# B4: the layout designer must retire itself when its layout dies (project
# clear, layout removal, direct destruction) instead of rendering a dead
# scene.
add_executable(test_layout_designer_lifecycle
  test_layout_designer_lifecycle.cpp
  ${CMAKE_SOURCE_DIR}/src/app/layout/qgslayoutdesignerdialog.cpp
)
target_link_libraries(test_layout_designer_lifecycle PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
  qgis_core
  qgis_gui
  sicnu_agent
)
target_include_directories(test_layout_designer_lifecycle PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
sicnu_link_jsoncpp(test_layout_designer_lifecycle)
set_target_properties(test_layout_designer_lifecycle PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_layout_designer_lifecycle)

# B2: the secondary view session (widget + engine view + pixel sync) —
# close→reopen must re-create the sync controller and land on a fresh
# engine view without duplicated connections.
add_executable(test_secondary_map_view_session
  test_secondary_map_view_session.cpp
  ${CMAKE_SOURCE_DIR}/src/app/shell/secondary_map_view_session.cpp
  ${CMAKE_SOURCE_DIR}/src/app/shell/secondary_map_view_widget.cpp
  ${CMAKE_SOURCE_DIR}/src/app/shell/rs_dual_viewport_sync_controller.cpp
)
target_link_libraries(test_secondary_map_view_session PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
  Sicnu::qgis_display
  qgis_core
  qgis_gui
)
target_include_directories(test_secondary_map_view_session PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
sicnu_link_jsoncpp(test_secondary_map_view_session)
set_target_properties(test_secondary_map_view_session PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_secondary_map_view_session)

# Scoped SpatialTool registrations: the process-wide registry gains
# unregister/token semantics so session hosts release their tools.
add_executable(test_spatial_tool_registration
  test_spatial_tool_registration.cpp
  ${CMAKE_SOURCE_DIR}/src/app/workbench/agent_context_tool.cpp
)
target_link_libraries(test_spatial_tool_registration PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  sicnu_agent
)
target_include_directories(test_spatial_tool_registration PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
sicnu_link_jsoncpp(test_spatial_tool_registration)
set_target_properties(test_spatial_tool_registration PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_spatial_tool_registration)
# ── Explainable Workflow (RS14-15): live adapters + guidance corpus ───────
# The explain core (sicnu_explain) stays Qt/engine/operator free; these tests
# cover the adapter layer that attaches authoritative sources to it, the
# authored guidance corpus on disk, and the consumers (builder/validator/
# view model/agent tool) that were designed in
# .planning/rs14-explainable-workflow/slices.md (Slices B–G).
function(sicnu_add_explain_test NAME)
  add_executable(${NAME} ${NAME}.cpp)
  target_link_libraries(${NAME} PRIVATE
    Catch2::Catch2WithMain
    Sicnu::explain
  )
  sicnu_link_jsoncpp(${NAME})
  target_include_directories(${NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
  )
  set_target_properties(${NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
  )
  sicnu_discover_tests(${NAME} TEST_PREFIX "${NAME}::")
endfunction()

# Core semantics over fakes: builder composition, validator matrix, guidance
# store loader, deterministic view model.
sicnu_add_explain_test(test_explain_builder)
sicnu_add_explain_test(test_explain_validator)
sicnu_add_explain_test(test_explain_guidance_store)
sicnu_add_explain_test(test_explain_view_model)

# Live adapters: real RSOperatorRegistry, real WorkflowDocument/StepDef IR,
# real provenance_<runId>.json envelopes.
add_executable(test_explain_registry_adapter test_explain_registry_adapter.cpp)
target_link_libraries(test_explain_registry_adapter PRIVATE
  Catch2::Catch2WithMain
  Sicnu::explain_adapters
)
sicnu_link_jsoncpp(test_explain_registry_adapter)
target_include_directories(test_explain_registry_adapter PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_explain_registry_adapter PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
)
sicnu_discover_tests(test_explain_registry_adapter TEST_PREFIX "test_explain_registry_adapter::")

add_executable(test_explain_projection test_explain_projection.cpp)
target_link_libraries(test_explain_projection PRIVATE
  Catch2::Catch2WithMain
  Sicnu::explain_adapters
  Qt6::Core
)
sicnu_link_jsoncpp(test_explain_projection)
target_include_directories(test_explain_projection PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_explain_projection PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
)
sicnu_discover_tests(test_explain_projection TEST_PREFIX "test_explain_projection::")

# Coverage oracle: the authored guidance corpus must stay in lockstep with
# the live registry (operator ids resolve, rationale parameters exist in the
# live schema).
add_executable(test_explain_guidance_coverage test_explain_guidance_coverage.cpp)
target_link_libraries(test_explain_guidance_coverage PRIVATE
  Catch2::Catch2WithMain
  Sicnu::explain_adapters
)
sicnu_link_jsoncpp(test_explain_guidance_coverage)
target_include_directories(test_explain_guidance_coverage PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
target_compile_definitions(test_explain_guidance_coverage PRIVATE
  SICNU_EXPLAIN_GUIDANCE_DIR="${CMAKE_SOURCE_DIR}/data/explain/guidance"
)
set_target_properties(test_explain_guidance_coverage PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
)
sicnu_discover_tests(test_explain_guidance_coverage TEST_PREFIX "test_explain_guidance_coverage::")

# explain:step tool logic: the tool TU is compiled directly (it holds no
# SpatialToolRegistry symbols by design); registry registration and catalog
# visibility are exercised by the heavy agent tool lane (test_agent_tools_3).
add_executable(test_explain_agent_tool
  test_explain_agent_tool.cpp
  ${CMAKE_SOURCE_DIR}/src/agent/spatial_tools/explain_step_tool.cpp
)
target_link_libraries(test_explain_agent_tool PRIVATE
  Catch2::Catch2WithMain
  Sicnu::explain_adapters
  Qt6::Core
)
sicnu_link_jsoncpp(test_explain_agent_tool)
target_include_directories(test_explain_agent_tool PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
target_compile_definitions(test_explain_agent_tool PRIVATE
  SICNU_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
set_target_properties(test_explain_agent_tool PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
)
sicnu_discover_tests(test_explain_agent_tool TEST_PREFIX "test_explain_agent_tool::")
# Completion 13/15: the in-process plugin model runtime bridge — first
# direct coverage of the IModelRuntime forward-serialization contract at
# the plugin adapter gate (model_runtime.h: implementations serialize).
# Narrow target: links only the bridge + operators seams, never the app.
if(SICNU_HAS_OPENCV AND OpenCV_FOUND)
  add_executable(test_plugin_model_bridge test_plugin_model_bridge.cpp)
  target_link_libraries(test_plugin_model_bridge PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    sicnu_plugins_framework
    sicnu_operators
    ${OpenCV_LIBS}
  )
  sicnu_link_jsoncpp(test_plugin_model_bridge)
  target_include_directories(test_plugin_model_bridge PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/tests
    ${CMAKE_BINARY_DIR}
  )
  sicnu_discover_tests(test_plugin_model_bridge)
endif()
# RS14-09 Scientific Task Planner recovery (PR #1193 implementation never
# landed; this restores it on the current contracts/capability API).
# Qt-free leaves: Catch2 + sicnu_planner (jsoncpp + sicnu_contracts only) —
# no Qt, no QGIS — so planner suites build and run in seconds.
add_executable(test_scientific_planner_vocab test_scientific_planner_vocab.cpp)
target_link_libraries(test_scientific_planner_vocab PRIVATE
  Catch2::Catch2WithMain
  sicnu_planner
)
target_include_directories(test_scientific_planner_vocab PRIVATE ${CMAKE_SOURCE_DIR}/src)
sicnu_discover_tests(test_scientific_planner_vocab TEST_PREFIX "test_scientific_planner_vocab::")

add_executable(test_scientific_planner_schema test_scientific_planner_schema.cpp)
target_link_libraries(test_scientific_planner_schema PRIVATE
  Catch2::Catch2WithMain
  sicnu_planner
)
target_include_directories(test_scientific_planner_schema PRIVATE ${CMAKE_SOURCE_DIR}/src)
sicnu_discover_tests(test_scientific_planner_schema TEST_PREFIX "test_scientific_planner_schema::")

add_executable(test_scientific_planner_core test_scientific_planner_core.cpp)
target_link_libraries(test_scientific_planner_core PRIVATE
  Catch2::Catch2WithMain
  sicnu_planner
)
target_include_directories(test_scientific_planner_core PRIVATE ${CMAKE_SOURCE_DIR}/src)
sicnu_discover_tests(test_scientific_planner_core TEST_PREFIX "test_scientific_planner_core::")

add_executable(test_scientific_planner_constraints test_scientific_planner_constraints.cpp)
target_link_libraries(test_scientific_planner_constraints PRIVATE
  Catch2::Catch2WithMain
  sicnu_planner
)
target_include_directories(test_scientific_planner_constraints PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(test_scientific_planner_constraints PRIVATE CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
sicnu_discover_tests(test_scientific_planner_constraints TEST_PREFIX "test_scientific_planner_constraints::")

add_executable(test_scientific_planner_teaching test_scientific_planner_teaching.cpp)
target_link_libraries(test_scientific_planner_teaching PRIVATE
  Catch2::Catch2WithMain
  sicnu_planner
)
target_include_directories(test_scientific_planner_teaching PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(test_scientific_planner_teaching PRIVATE CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
sicnu_discover_tests(test_scientific_planner_teaching TEST_PREFIX "test_scientific_planner_teaching::")

add_executable(test_scientific_planner_proposal test_scientific_planner_proposal.cpp)
target_link_libraries(test_scientific_planner_proposal PRIVATE
  Catch2::Catch2WithMain
  sicnu_planner
)
target_include_directories(test_scientific_planner_proposal PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(test_scientific_planner_proposal PRIVATE CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
sicnu_discover_tests(test_scientific_planner_proposal TEST_PREFIX "test_scientific_planner_proposal::")

add_executable(test_scientific_planner_ir test_scientific_planner_ir.cpp)
target_link_libraries(test_scientific_planner_ir PRIVATE
  Catch2::Catch2WithMain
  sicnu_planner
)
target_include_directories(test_scientific_planner_ir PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(test_scientific_planner_ir PRIVATE CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
sicnu_discover_tests(test_scientific_planner_ir TEST_PREFIX "test_scientific_planner_ir::")

add_executable(test_scientific_planner_golden test_scientific_planner_golden.cpp)
target_link_libraries(test_scientific_planner_golden PRIVATE
  Catch2::Catch2WithMain
  sicnu_planner
)
target_include_directories(test_scientific_planner_golden PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(test_scientific_planner_golden PRIVATE CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
sicnu_discover_tests(test_scientific_planner_golden TEST_PREFIX "test_scientific_planner_golden::")

add_executable(test_scientific_planner_drift test_scientific_planner_drift.cpp)
target_link_libraries(test_scientific_planner_drift PRIVATE
  Catch2::Catch2WithMain
  sicnu_planner
  sicnu_scientific_state
)
target_include_directories(test_scientific_planner_drift PRIVATE ${CMAKE_SOURCE_DIR}/src)
sicnu_discover_tests(test_scientific_planner_drift TEST_PREFIX "test_scientific_planner_drift::")

add_executable(test_scientific_planner_review test_scientific_planner_review.cpp)
target_link_libraries(test_scientific_planner_review PRIVATE
  Catch2::Catch2WithMain
  sicnu_planner
)
target_include_directories(test_scientific_planner_review PRIVATE ${CMAKE_SOURCE_DIR}/src)
sicnu_discover_tests(test_scientific_planner_review TEST_PREFIX "test_scientific_planner_review::")
# RS14-02 Scientific Preflight Engine slice B: deterministic rule engine,
# builtin rule families, provider seam and teaching/agent projections —
# Qt/GDAL-free leaf tests over sicnu_preflight; the provider test adds the
# sicnu_preflight_state_adapter (passport projection) over sicnu_scientific_state.
add_executable(test_preflight_engine test_preflight_engine.cpp)
target_link_libraries(test_preflight_engine PRIVATE
  Catch2::Catch2WithMain
  sicnu_preflight
)
sicnu_discover_tests(test_preflight_engine TEST_PREFIX "test_preflight_engine::")

add_executable(test_preflight_rules test_preflight_rules.cpp)
target_link_libraries(test_preflight_rules PRIVATE
  Catch2::Catch2WithMain
  sicnu_preflight
)
sicnu_discover_tests(test_preflight_rules TEST_PREFIX "test_preflight_rules::")

add_executable(test_preflight_render test_preflight_render.cpp)
target_link_libraries(test_preflight_render PRIVATE
  Catch2::Catch2WithMain
  sicnu_preflight
)
sicnu_discover_tests(test_preflight_render TEST_PREFIX "test_preflight_render::")

add_executable(test_preflight_provider test_preflight_provider.cpp)
target_compile_definitions(test_preflight_provider PRIVATE
  SICNU_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
target_link_libraries(test_preflight_provider PRIVATE
  Catch2::Catch2WithMain
  sicnu_preflight_state_adapter
  sicnu_scientific_state
)
sicnu_discover_tests(test_preflight_provider TEST_PREFIX "test_preflight_provider::")

add_executable(test_preflight_golden test_preflight_golden.cpp)
target_link_libraries(test_preflight_golden PRIVATE
  Catch2::Catch2WithMain
  sicnu_preflight
)
sicnu_discover_tests(test_preflight_golden TEST_PREFIX "test_preflight_golden::")
