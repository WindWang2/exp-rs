# SICNU GEO RS — Unit Tests (Catch2)
include(Catch)

if(WIN32 AND TARGET Catch2WithMain)
  set_property(TARGET Catch2WithMain PROPERTY SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/sicnu_test_main.cpp")
endif()

# Wrapper around catch_discover_tests that injects LSAN_OPTIONS when sanitizers
# are enabled, suppressing known singleton leaks (QgsExpression functions,
# QgsNumericFormatRegistry, Nvidia EGL driver). Callers may pass extra
# test PROPERTIES (e.g. RUN_SERIAL TRUE) after the NAME.
function(sicnu_discover_tests NAME)
  set(_discovery_props "")
  if(ENABLE_SANITIZERS)
    # PRE_TEST mode runs test discovery at ctest time (not build time), so
    # ENVIRONMENT_MODIFICATION is applied to the discovery process.
    # This prevents ASAN leak reports from failing test discovery.
    list(APPEND _discovery_props "ENVIRONMENT_MODIFICATION" "LSAN_OPTIONS=set:detect_leaks=0")
  endif()
  if(ARGN)
    # ARGN is a flat list of key/value test properties (RUN_SERIAL TRUE, ...).
    list(APPEND _discovery_props ${ARGN})
  endif()
  # PRE_TEST avoids race conditions in parallel builds where discovery
  # tries to run the test executable before it is linked.
  catch_discover_tests(${NAME}
    DISCOVERY_MODE PRE_TEST
    PROPERTIES ${_discovery_props})
endfunction()

# Helper function to add a test executable
function(sicnu_add_test NAME)
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
  )
  target_include_directories(${NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/stubs
    ${CMAKE_BINARY_DIR}
  )
  target_compile_definitions(${NAME} PRIVATE
    CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
  )
  sicnu_discover_tests(${NAME})
endfunction()

# Smoke tests
sicnu_add_test(test_smoke)
sicnu_add_test(test_crs)
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
  sicnu_operators
)
if(TARGET jsoncpp)
  target_link_libraries(test_tool_call_dispatcher PRIVATE jsoncpp)
elseif(JSONCPP_LIBRARIES)
  target_include_directories(test_tool_call_dispatcher PRIVATE ${JSONCPP_INCLUDE_DIRS})
  target_link_libraries(test_tool_call_dispatcher PRIVATE ${JSONCPP_LIBRARIES})
else()
  target_link_libraries(test_tool_call_dispatcher PRIVATE jsoncpp)
endif()
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
if(TARGET jsoncpp)
  target_link_libraries(test_execution_plane PRIVATE jsoncpp)
elseif(JSONCPP_LIBRARIES)
  target_include_directories(test_execution_plane PRIVATE ${JSONCPP_INCLUDE_DIRS})
  target_link_libraries(test_execution_plane PRIVATE ${JSONCPP_LIBRARIES})
else()
  target_link_libraries(test_execution_plane PRIVATE jsoncpp)
endif()
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

# #35 — UI callers must not bypass Task Center; task views are projections.
add_executable(test_ui_task_center_contract
  test_ui_task_center_contract.cpp
  # AsyncAlgorithmRunner's implementation is compiled directly into the test
  # (it is a deprecated legacy type not built into any library; same pattern as
  # test_task_center_dock compiling its panel source). async_runner_base.cpp is
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

add_executable(test_task_center_dock
    test_task_center_dock.cpp
    ${CMAKE_SOURCE_DIR}/src/app/panels/task_center_dock.cpp
)
target_link_libraries(test_task_center_dock PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    qgis_core
    qgis_gui
    sicnu_processing
    sicnu_task_center
)
target_include_directories(test_task_center_dock PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_task_center_dock)

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

# Refactor tests — verify main.cpp split structure
sicnu_add_test(test_refactor)

# Consolidation tests — verify provider merge
add_executable(test_consolidate test_consolidate.cpp)
target_link_libraries(test_consolidate PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
)
target_include_directories(test_consolidate PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_consolidate)

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

# Agent run coordinator state machine + bounded repair
add_executable(test_agent_run_coordinator test_agent_run_coordinator.cpp)
target_link_libraries(test_agent_run_coordinator PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Test
  qgis_core
  sicnu_agent
  sicnu_processing
  sicnu_task_center
)
target_include_directories(test_agent_run_coordinator PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_agent_run_coordinator)

# Closed-loop gap closure (Tester C): repairable/unrepairable, retry limit,
# verification gate, invalid output, registration, layer load, final map,
# GUI close, inspector transitions — QTemporaryDir + heap QApplication
add_executable(test_agent_closed_loop test_agent_closed_loop.cpp)
target_link_libraries(test_agent_closed_loop PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
  qgis_core
  qgis_gui
  sicnu_agent
  sicnu_data
  sicnu_processing
  sicnu_task_center
  sicnu_jobs
  sicnu_operators
  GDAL::GDAL
)
if(TARGET jsoncpp)
  target_link_libraries(test_agent_closed_loop PRIVATE jsoncpp)
elseif(JSONCPP_LIBRARIES)
  target_include_directories(test_agent_closed_loop PRIVATE ${JSONCPP_INCLUDE_DIRS})
  target_link_libraries(test_agent_closed_loop PRIVATE ${JSONCPP_LIBRARIES})
else()
  target_link_libraries(test_agent_closed_loop PRIVATE jsoncpp)
endif()
target_include_directories(test_agent_closed_loop PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_agent_closed_loop)

# Model catalog v2 contracts: readiness states, checksums, ranking gates
sicnu_add_test(test_model_catalog_v2)

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
    if(TARGET jsoncpp)
        target_link_libraries(test_model_runtime PRIVATE jsoncpp)
    elseif(JSONCPP_LIBRARIES)
        target_include_directories(test_model_runtime PRIVATE ${JSONCPP_INCLUDE_DIRS})
        target_link_libraries(test_model_runtime PRIVATE ${JSONCPP_LIBRARIES})
    else()
        target_link_libraries(test_model_runtime PRIVATE jsoncpp)
    endif()
    target_include_directories(test_model_runtime PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}
    )
    sicnu_discover_tests(test_model_runtime)
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
sicnu_add_test(test_perf_benchmarks)

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
add_executable(test_preferences_dialog test_preferences_dialog.cpp)
target_link_libraries(test_preferences_dialog PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
)
target_include_directories(test_preferences_dialog PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
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
if(TARGET jsoncpp)
  target_link_libraries(test_change_detection PRIVATE jsoncpp)
elseif(JSONCPP_LIBRARIES)
  target_include_directories(test_change_detection PRIVATE ${JSONCPP_INCLUDE_DIRS})
  target_link_libraries(test_change_detection PRIVATE ${JSONCPP_LIBRARIES})
else()
  target_link_libraries(test_change_detection PRIVATE jsoncpp)
endif()
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
  sicnu_discover_tests(test_classifier_kmeans)

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

  add_executable(test_obia_task
    test_obia_task.cpp
    ${CMAKE_SOURCE_DIR}/src/app/shell/gui_job_adapter.cpp
  )
  target_link_libraries(test_obia_task PRIVATE
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
  target_include_directories(test_obia_task PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/analysis
    ${CMAKE_SOURCE_DIR}/src/analysis/classification
    ${CMAKE_SOURCE_DIR}/src/analysis/segmentation
    ${CMAKE_SOURCE_DIR}/src/app/obia
    ${OpenCV_INCLUDE_DIRS}
    ${CMAKE_BINARY_DIR}
  )
  set_target_properties(test_obia_task PROPERTIES AUTOMOC ON)
  sicnu_discover_tests(test_obia_task)

  # Phase 10B.6 — OBIA end-to-end integration tests
  add_executable(test_obia_integration
    test_obia_integration.cpp
    ${CMAKE_SOURCE_DIR}/src/app/shell/gui_job_adapter.cpp
  )
  target_link_libraries(test_obia_integration PRIVATE
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
  if(TARGET jsoncpp)
    target_link_libraries(test_pipeline_runner PRIVATE jsoncpp)
  elseif(JSONCPP_LIBRARIES)
    target_include_directories(test_pipeline_runner PRIVATE ${JSONCPP_INCLUDE_DIRS})
    target_link_libraries(test_pipeline_runner PRIVATE ${JSONCPP_LIBRARIES})
  else()
    target_link_libraries(test_pipeline_runner PRIVATE jsoncpp)
  endif()
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
  if(TARGET jsoncpp)
    target_link_libraries(test_rs_operator PRIVATE jsoncpp)
  elseif(JSONCPP_LIBRARIES)
    target_include_directories(test_rs_operator PRIVATE ${JSONCPP_INCLUDE_DIRS})
    target_link_libraries(test_rs_operator PRIVATE ${JSONCPP_LIBRARIES})
  else()
    target_link_libraries(test_rs_operator PRIVATE jsoncpp)
  endif()
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
  if(TARGET jsoncpp)
    target_link_libraries(test_rs_operators PRIVATE jsoncpp)
  elseif(JSONCPP_LIBRARIES)
    target_include_directories(test_rs_operators PRIVATE ${JSONCPP_INCLUDE_DIRS})
    target_link_libraries(test_rs_operators PRIVATE ${JSONCPP_LIBRARIES})
  else()
    target_link_libraries(test_rs_operators PRIVATE jsoncpp)
  endif()
  sicnu_discover_tests(test_rs_operators)

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
  if(TARGET jsoncpp)
    target_link_libraries(test_otb_operators PRIVATE jsoncpp)
  elseif(JSONCPP_LIBRARIES)
    target_include_directories(test_otb_operators PRIVATE ${JSONCPP_INCLUDE_DIRS})
    target_link_libraries(test_otb_operators PRIVATE ${JSONCPP_LIBRARIES})
  else()
    target_link_libraries(test_otb_operators PRIVATE jsoncpp)
  endif()
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
  if(TARGET jsoncpp)
    target_link_libraries(test_operation_logger PRIVATE jsoncpp)
  elseif(JSONCPP_LIBRARIES)
    target_include_directories(test_operation_logger PRIVATE ${JSONCPP_INCLUDE_DIRS})
    target_link_libraries(test_operation_logger PRIVATE ${JSONCPP_LIBRARIES})
  else()
    target_link_libraries(test_operation_logger PRIVATE jsoncpp)
  endif()
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
  if(TARGET jsoncpp)
    target_link_libraries(test_gdal_ortho_operators PRIVATE jsoncpp)
  elseif(JSONCPP_LIBRARIES)
    target_include_directories(test_gdal_ortho_operators PRIVATE ${JSONCPP_INCLUDE_DIRS})
    target_link_libraries(test_gdal_ortho_operators PRIVATE ${JSONCPP_LIBRARIES})
  else()
    target_link_libraries(test_gdal_ortho_operators PRIVATE jsoncpp)
  endif()
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
  if(TARGET jsoncpp)
    target_link_libraries(test_qa_mask PRIVATE jsoncpp)
  elseif(JSONCPP_LIBRARIES)
    target_include_directories(test_qa_mask PRIVATE ${JSONCPP_INCLUDE_DIRS})
    target_link_libraries(test_qa_mask PRIVATE ${JSONCPP_LIBRARIES})
  else()
    target_link_libraries(test_qa_mask PRIVATE jsoncpp)
  endif()
  sicnu_discover_tests(test_qa_mask)

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
  if(TARGET jsoncpp)
    target_link_libraries(test_mnf PRIVATE jsoncpp)
  elseif(JSONCPP_LIBRARIES)
    target_include_directories(test_mnf PRIVATE ${JSONCPP_INCLUDE_DIRS})
    target_link_libraries(test_mnf PRIVATE ${JSONCPP_LIBRARIES})
  else()
    target_link_libraries(test_mnf PRIVATE jsoncpp)
  endif()
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
  if(TARGET jsoncpp)
    target_link_libraries(test_spectral_unmixing PRIVATE jsoncpp)
  elseif(JSONCPP_LIBRARIES)
    target_include_directories(test_spectral_unmixing PRIVATE ${JSONCPP_INCLUDE_DIRS})
    target_link_libraries(test_spectral_unmixing PRIVATE ${JSONCPP_LIBRARIES})
  else()
    target_link_libraries(test_spectral_unmixing PRIVATE jsoncpp)
  endif()
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
  if(TARGET jsoncpp)
    target_link_libraries(test_spectral_anomaly PRIVATE jsoncpp)
  elseif(JSONCPP_LIBRARIES)
    target_include_directories(test_spectral_anomaly PRIVATE ${JSONCPP_INCLUDE_DIRS})
    target_link_libraries(test_spectral_anomaly PRIVATE ${JSONCPP_LIBRARIES})
  else()
    target_link_libraries(test_spectral_anomaly PRIVATE jsoncpp)
  endif()
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
  if(TARGET jsoncpp)
    target_link_libraries(test_spectral_resampling PRIVATE jsoncpp)
  elseif(JSONCPP_LIBRARIES)
    target_include_directories(test_spectral_resampling PRIVATE ${JSONCPP_INCLUDE_DIRS})
    target_link_libraries(test_spectral_resampling PRIVATE ${JSONCPP_LIBRARIES})
  else()
    target_link_libraries(test_spectral_resampling PRIVATE jsoncpp)
  endif()
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
  if(TARGET jsoncpp)
    target_link_libraries(test_endmember_extraction PRIVATE jsoncpp)
  elseif(JSONCPP_LIBRARIES)
    target_include_directories(test_endmember_extraction PRIVATE ${JSONCPP_INCLUDE_DIRS})
    target_link_libraries(test_endmember_extraction PRIVATE ${JSONCPP_LIBRARIES})
  else()
    target_link_libraries(test_endmember_extraction PRIVATE jsoncpp)
  endif()
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
    if(TARGET jsoncpp)
      target_link_libraries(test_opencv_operators PRIVATE jsoncpp)
    elseif(JSONCPP_LIBRARIES)
      target_include_directories(test_opencv_operators PRIVATE ${JSONCPP_INCLUDE_DIRS})
      target_link_libraries(test_opencv_operators PRIVATE ${JSONCPP_LIBRARIES})
    else()
      target_link_libraries(test_opencv_operators PRIVATE jsoncpp)
    endif()
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

# Phase 10B.0 — OTB smoke test (guarded by SICNU_HAS_OTB)
if(SICNU_HAS_OTB)
  add_executable(test_otb_smoke test_otb_smoke.cpp)
  target_link_libraries(test_otb_smoke PRIVATE
    OTBCommon
    Catch2::Catch2WithMain
  )
  target_include_directories(test_otb_smoke PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
  )
  target_compile_definitions(test_otb_smoke PRIVATE SICNU_HAS_OTB=1)
  sicnu_discover_tests(test_otb_smoke)
endif()

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
add_executable(test_raster_processing_dialog_base
  test_raster_processing_dialog_base.cpp
  test_raster_processing_dialog_base_main.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/raster_processing_dialog_base.cpp
  ${CMAKE_SOURCE_DIR}/src/app/shell/processing_job_adapter.cpp
  ${CMAKE_SOURCE_DIR}/src/app/shell/gui_job_adapter.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/dialog_help_catalog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/dialog_utils.cpp
)
set_target_properties(test_raster_processing_dialog_base PROPERTIES AUTOMOC ON)
target_link_libraries(test_raster_processing_dialog_base PRIVATE
  Catch2::Catch2
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  sicnu_jobs
  sicnu_operators
  sicnu_task_center

  Qt6::Concurrent
  qgis_core
  qgis_gui
  sicnu_data
)
target_include_directories(test_raster_processing_dialog_base PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_raster_processing_dialog_base)

# Workflow engine runtime smoke test (Qt-free headers; may link Qt6::Core via sicnu_operators)
if(TARGET sicnu_workflow)
  add_executable(test_workflow_runtime test_workflow_runtime.cpp)
  target_link_libraries(test_workflow_runtime PRIVATE
    Catch2::Catch2WithMain
    sicnu_workflow
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
  sicnu_task_center
  sicnu_core
  Sicnu::data
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

# Data Manager panel tests — asset catalog projection, separate from layer tree
add_executable(test_data_manager_panel
  test_data_manager_panel.cpp
  ${CMAKE_SOURCE_DIR}/src/app/panels/data_manager_panel.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/dialog_help_catalog.cpp
)
target_link_libraries(test_data_manager_panel PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
  qgis_core
  qgis_gui
  Sicnu::qgis_display
)
target_include_directories(test_data_manager_panel PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_data_manager_panel PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_data_manager_panel)

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
if(TARGET jsoncpp)
  target_link_libraries(test_satellite_products PRIVATE jsoncpp)
elseif(JSONCPP_LIBRARIES)
  target_include_directories(test_satellite_products PRIVATE ${JSONCPP_INCLUDE_DIRS})
  target_link_libraries(test_satellite_products PRIVATE ${JSONCPP_LIBRARIES})
else()
  target_link_libraries(test_satellite_products PRIVATE jsoncpp)
endif()
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
add_executable(test_orthorectification_dialog
  test_orthorectification_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/orthorectification_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/raster_processing_dialog_base.cpp
  ${CMAKE_SOURCE_DIR}/src/app/shell/processing_job_adapter.cpp
  ${CMAKE_SOURCE_DIR}/src/app/shell/gui_job_adapter.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/dialog_help_catalog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/dialog_utils.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/crs_selector.cpp
)
target_link_libraries(test_orthorectification_dialog PRIVATE
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
target_include_directories(test_orthorectification_dialog PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
if(TARGET jsoncpp)
  target_link_libraries(test_orthorectification_dialog PRIVATE jsoncpp)
elseif(JSONCPP_LIBRARIES)
  target_include_directories(test_orthorectification_dialog PRIVATE ${JSONCPP_INCLUDE_DIRS})
  target_link_libraries(test_orthorectification_dialog PRIVATE ${JSONCPP_LIBRARIES})
else()
  target_link_libraries(test_orthorectification_dialog PRIVATE jsoncpp)
endif()
set_target_properties(test_orthorectification_dialog PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_orthorectification_dialog)

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
if(TARGET jsoncpp)
  target_link_libraries(test_batch_processing_dialog PRIVATE jsoncpp)
elseif(JSONCPP_LIBRARIES)
  target_include_directories(test_batch_processing_dialog PRIVATE ${JSONCPP_INCLUDE_DIRS})
  target_link_libraries(test_batch_processing_dialog PRIVATE ${JSONCPP_LIBRARIES})
else()
  target_link_libraries(test_batch_processing_dialog PRIVATE jsoncpp)
endif()
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
if(TARGET jsoncpp)
  target_link_libraries(test_grid_compat_dialog_utils PRIVATE jsoncpp)
elseif(JSONCPP_LIBRARIES)
  target_include_directories(test_grid_compat_dialog_utils PRIVATE ${JSONCPP_INCLUDE_DIRS})
  target_link_libraries(test_grid_compat_dialog_utils PRIVATE ${JSONCPP_LIBRARIES})
else()
  target_link_libraries(test_grid_compat_dialog_utils PRIVATE jsoncpp)
endif()
set_target_properties(test_grid_compat_dialog_utils PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_grid_compat_dialog_utils)

# Change-detection dialog — method/mask parameter alignment with the backend
add_executable(test_change_detection_dialog
  test_change_detection_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/change_detection_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/comparison_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/comparison_widget.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/raster_processing_dialog_base.cpp
  ${CMAKE_SOURCE_DIR}/src/app/shell/processing_job_adapter.cpp
  ${CMAKE_SOURCE_DIR}/src/app/shell/gui_job_adapter.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/dialog_help_catalog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/dialog_utils.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/raster_layer_combo.cpp
)
target_link_libraries(test_change_detection_dialog PRIVATE
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
target_include_directories(test_change_detection_dialog PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
if(TARGET jsoncpp)
  target_link_libraries(test_change_detection_dialog PRIVATE jsoncpp)
elseif(JSONCPP_LIBRARIES)
  target_include_directories(test_change_detection_dialog PRIVATE ${JSONCPP_INCLUDE_DIRS})
  target_link_libraries(test_change_detection_dialog PRIVATE ${JSONCPP_LIBRARIES})
else()
  target_link_libraries(test_change_detection_dialog PRIVATE jsoncpp)
endif()
set_target_properties(test_change_detection_dialog PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_change_detection_dialog)

# Apply-mask dialog — operator JSON assembly from widget state
add_executable(test_apply_mask_dialog
  test_apply_mask_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/apply_mask_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/raster_processing_dialog_base.cpp
  ${CMAKE_SOURCE_DIR}/src/app/shell/processing_job_adapter.cpp
  ${CMAKE_SOURCE_DIR}/src/app/shell/gui_job_adapter.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/dialog_help_catalog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/dialog_utils.cpp
)
target_link_libraries(test_apply_mask_dialog PRIVATE
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
target_include_directories(test_apply_mask_dialog PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
if(TARGET jsoncpp)
  target_link_libraries(test_apply_mask_dialog PRIVATE jsoncpp)
elseif(JSONCPP_LIBRARIES)
  target_include_directories(test_apply_mask_dialog PRIVATE ${JSONCPP_INCLUDE_DIRS})
  target_link_libraries(test_apply_mask_dialog PRIVATE ${JSONCPP_LIBRARIES})
else()
  target_link_libraries(test_apply_mask_dialog PRIVATE jsoncpp)
endif()
set_target_properties(test_apply_mask_dialog PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_apply_mask_dialog)

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
if(TARGET jsoncpp)
  target_link_libraries(test_spectral_library_dialog PRIVATE jsoncpp)
elseif(JSONCPP_LIBRARIES)
  target_include_directories(test_spectral_library_dialog PRIVATE ${JSONCPP_INCLUDE_DIRS})
  target_link_libraries(test_spectral_library_dialog PRIVATE ${JSONCPP_LIBRARIES})
else()
  target_link_libraries(test_spectral_library_dialog PRIVATE jsoncpp)
endif()
set_target_properties(test_spectral_library_dialog PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_spectral_library_dialog)

# Post-classification comparison dialog — operator JSON assembly
add_executable(test_post_classification_dialog
  test_post_classification_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/post_classification_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/raster_processing_dialog_base.cpp
  ${CMAKE_SOURCE_DIR}/src/app/shell/processing_job_adapter.cpp
  ${CMAKE_SOURCE_DIR}/src/app/shell/gui_job_adapter.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/dialog_help_catalog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/dialog_utils.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/raster_layer_combo.cpp
)
target_link_libraries(test_post_classification_dialog PRIVATE
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
target_include_directories(test_post_classification_dialog PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
if(TARGET jsoncpp)
  target_link_libraries(test_post_classification_dialog PRIVATE jsoncpp)
elseif(JSONCPP_LIBRARIES)
  target_include_directories(test_post_classification_dialog PRIVATE ${JSONCPP_INCLUDE_DIRS})
  target_link_libraries(test_post_classification_dialog PRIVATE ${JSONCPP_LIBRARIES})
else()
  target_link_libraries(test_post_classification_dialog PRIVATE jsoncpp)
endif()
set_target_properties(test_post_classification_dialog PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_post_classification_dialog)

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

# Spectral index dialog — role-based band preselection via BandRoleCombo (C5)
add_executable(test_spectral_index_dialog
  test_spectral_index_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/spectral_index_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/raster_processing_dialog_base.cpp
  ${CMAKE_SOURCE_DIR}/src/app/shell/processing_job_adapter.cpp
  ${CMAKE_SOURCE_DIR}/src/app/shell/gui_job_adapter.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/dialog_help_catalog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/dialog_utils.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/band_role_combo.cpp
)
target_link_libraries(test_spectral_index_dialog PRIVATE
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
target_include_directories(test_spectral_index_dialog PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
if(TARGET jsoncpp)
  target_link_libraries(test_spectral_index_dialog PRIVATE jsoncpp)
elseif(JSONCPP_LIBRARIES)
  target_include_directories(test_spectral_index_dialog PRIVATE ${JSONCPP_INCLUDE_DIRS})
  target_link_libraries(test_spectral_index_dialog PRIVATE ${JSONCPP_LIBRARIES})
else()
  target_link_libraries(test_spectral_index_dialog PRIVATE jsoncpp)
endif()
sicnu_discover_tests(test_spectral_index_dialog)

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
  sicnu_task_center
  sicnu_data
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
if(TARGET jsoncpp)
  target_link_libraries(test_rs_golden_e2e PRIVATE jsoncpp)
elseif(JSONCPP_LIBRARIES)
  target_include_directories(test_rs_golden_e2e PRIVATE ${JSONCPP_INCLUDE_DIRS})
  target_link_libraries(test_rs_golden_e2e PRIVATE ${JSONCPP_LIBRARIES})
else()
  target_link_libraries(test_rs_golden_e2e PRIVATE jsoncpp)
endif()
set_target_properties(test_rs_golden_e2e PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_rs_golden_e2e)

# ---------------------------------------------------------------------------
# Wave regression tests — newly landed, registered here
# ---------------------------------------------------------------------------

add_executable(test_g02_gdal_regression test_g02_gdal_regression.cpp)
target_link_libraries(test_g02_gdal_regression PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
  GDAL::GDAL
)
target_include_directories(test_g02_gdal_regression PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_g02_gdal_regression)

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

add_executable(test_w1_regression test_w1_regression.cpp
  ${CMAKE_SOURCE_DIR}/src/agent/mcp_server.cpp
  ${CMAKE_SOURCE_DIR}/src/app/shell/processing_job_adapter.cpp
  ${CMAKE_SOURCE_DIR}/src/cli/rs_pipeline_runner.cpp
)
target_link_libraries(test_w1_regression PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
  qgis_core
  qgis_gui
  sicnu_agent
  sicnu_processing
  sicnu_data
  Sicnu::data
  sicnu_task_center
  sicnu_workflow
  sicnu_operators
  sicnu_jobs
  GDAL::GDAL
)
if(TARGET Sicnu::qgis_display)
  target_link_libraries(test_w1_regression PRIVATE Sicnu::qgis_display)
endif()
if(TARGET jsoncpp)
  target_link_libraries(test_w1_regression PRIVATE jsoncpp)
elseif(JSONCPP_LIBRARIES)
  target_include_directories(test_w1_regression PRIVATE ${JSONCPP_INCLUDE_DIRS})
  target_link_libraries(test_w1_regression PRIVATE ${JSONCPP_LIBRARIES})
else()
  target_link_libraries(test_w1_regression PRIVATE jsoncpp)
endif()
target_include_directories(test_w1_regression PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_w1_regression PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_w1_regression)

add_executable(test_w3_provider_misc_regression test_w3_provider_misc_regression.cpp)
target_link_libraries(test_w3_provider_misc_regression PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  qgis_analysis
  sicnu_processing
  sicnu_operators
  GDAL::GDAL
)
if(TARGET jsoncpp)
  target_link_libraries(test_w3_provider_misc_regression PRIVATE jsoncpp)
elseif(JSONCPP_LIBRARIES)
  target_include_directories(test_w3_provider_misc_regression PRIVATE ${JSONCPP_INCLUDE_DIRS})
  target_link_libraries(test_w3_provider_misc_regression PRIVATE ${JSONCPP_LIBRARIES})
else()
  target_link_libraries(test_w3_provider_misc_regression PRIVATE jsoncpp)
endif()
target_include_directories(test_w3_provider_misc_regression PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_w3_provider_misc_regression)

add_executable(test_w4_group_split test_w4_group_split.cpp)
target_link_libraries(test_w4_group_split PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  qgis_core
  qgis_analysis
  sicnu_processing
  GDAL::GDAL
)
if(SICNU_HAS_OPENCV)
  target_link_libraries(test_w4_group_split PRIVATE ${OpenCV_LIBS})
  target_include_directories(test_w4_group_split PRIVATE ${OpenCV_INCLUDE_DIRS})
  target_compile_definitions(test_w4_group_split PRIVATE SICNU_HAS_OPENCV=1)
endif()
target_include_directories(test_w4_group_split PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/classification
  ${CMAKE_SOURCE_DIR}/src/analysis/georeferencing
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_w4_group_split)

add_executable(test_w4_kmeans_obia test_w4_kmeans_obia.cpp)
target_link_libraries(test_w4_kmeans_obia PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  qgis_analysis
)
if(SICNU_HAS_OPENCV)
  target_link_libraries(test_w4_kmeans_obia PRIVATE ${OpenCV_LIBS})
  target_include_directories(test_w4_kmeans_obia PRIVATE ${OpenCV_INCLUDE_DIRS})
  target_compile_definitions(test_w4_kmeans_obia PRIVATE SICNU_HAS_OPENCV=1)
endif()
target_include_directories(test_w4_kmeans_obia PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/classification
  ${CMAKE_SOURCE_DIR}/src/analysis/segmentation
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_w4_kmeans_obia)

add_executable(test_w4_mlp_rf_persist test_w4_mlp_rf_persist.cpp)
target_link_libraries(test_w4_mlp_rf_persist PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  qgis_analysis
)
if(SICNU_HAS_OPENCV)
  target_link_libraries(test_w4_mlp_rf_persist PRIVATE ${OpenCV_LIBS})
  target_include_directories(test_w4_mlp_rf_persist PRIVATE ${OpenCV_INCLUDE_DIRS})
  target_compile_definitions(test_w4_mlp_rf_persist PRIVATE SICNU_HAS_OPENCV=1)
endif()
target_include_directories(test_w4_mlp_rf_persist PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/classification
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_w4_mlp_rf_persist)

add_executable(test_w4_polygonize_atomic test_w4_polygonize_atomic.cpp)
target_link_libraries(test_w4_polygonize_atomic PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  qgis_core
  qgis_analysis
  GDAL::GDAL
)
target_include_directories(test_w4_polygonize_atomic PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/segmentation
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_w4_polygonize_atomic)

add_executable(test_w5a_radiometric_regression test_w5a_radiometric_regression.cpp)
target_link_libraries(test_w5a_radiometric_regression PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
  GDAL::GDAL
)
target_include_directories(test_w5a_radiometric_regression PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_w5a_radiometric_regression)

add_executable(test_w5b_numerics test_w5b_numerics.cpp)
target_link_libraries(test_w5b_numerics PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_processing
  GDAL::GDAL
)
target_include_directories(test_w5b_numerics PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_w5b_numerics)

add_executable(test_w6_georef_regressions test_w6_georef_regressions.cpp
  ${CMAKE_SOURCE_DIR}/src/app/crs_presets.cpp
)
target_link_libraries(test_w6_georef_regressions PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_analysis
  qgis_app_georef
  GDAL::GDAL
)
target_include_directories(test_w6_georef_regressions PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_SOURCE_DIR}/src/app/georeferencer
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/georeferencing
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_w6_georef_regressions PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_w6_georef_regressions)

add_executable(test_w8_obia_regression test_w8_obia_regression.cpp)
target_link_libraries(test_w8_obia_regression PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  qgis_core
  qgis_analysis
  qgis_app_georef
  GDAL::GDAL
  sicnu_data
)
if(SICNU_HAS_OPENCV)
  target_link_libraries(test_w8_obia_regression PRIVATE ${OpenCV_LIBS})
  target_include_directories(test_w8_obia_regression PRIVATE ${OpenCV_INCLUDE_DIRS})
endif()
target_include_directories(test_w8_obia_regression PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/analysis
  ${CMAKE_SOURCE_DIR}/src/analysis/segmentation
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_w8_obia_regression)

add_executable(test_w9_regressions test_w9_regressions.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/batch_processing_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/layout/qgslayoutdesignerdialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/raster_processing_dialog_base.cpp
  ${CMAKE_SOURCE_DIR}/src/app/shell/processing_job_adapter.cpp
  ${CMAKE_SOURCE_DIR}/src/app/shell/gui_job_adapter.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/dialog_help_catalog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/dialog_utils.cpp
)
target_link_libraries(test_w9_regressions PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
  Qt6::Concurrent
  qgis_core
  qgis_gui
  qgis_analysis
  qgis_app_georef
  sicnu_processing
  sicnu_operators
  sicnu_data
  sicnu_jobs
  sicnu_task_center
  GDAL::GDAL
)
if(TARGET jsoncpp)
  target_link_libraries(test_w9_regressions PRIVATE jsoncpp)
elseif(JSONCPP_LIBRARIES)
  target_include_directories(test_w9_regressions PRIVATE ${JSONCPP_INCLUDE_DIRS})
  target_link_libraries(test_w9_regressions PRIVATE ${JSONCPP_LIBRARIES})
else()
  target_link_libraries(test_w9_regressions PRIVATE jsoncpp)
endif()
target_include_directories(test_w9_regressions PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_w9_regressions PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_w9_regressions)

add_executable(test_w10_ui_misc_regression test_w10_ui_misc_regression.cpp
  ${CMAKE_SOURCE_DIR}/src/app/shell/schema_form_builder.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/dialog_help_catalog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/dialog_utils.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/post_classification_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/qa_mask_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/change_detection_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/comparison_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/comparison_widget.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/raster_processing_dialog_base.cpp
  ${CMAKE_SOURCE_DIR}/src/app/shell/processing_job_adapter.cpp
  ${CMAKE_SOURCE_DIR}/src/app/shell/gui_job_adapter.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/raster_layer_combo.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/band_role_combo.cpp
  ${CMAKE_SOURCE_DIR}/src/app/dialogs/preferences_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/app/qgis_app_facade.cpp
  ${CMAKE_SOURCE_DIR}/src/app/qgsclipboard.cpp
)
target_link_libraries(test_w10_ui_misc_regression PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Test
  Qt6::Concurrent
  qgis_core
  qgis_gui
  qgis_analysis
  qgis_app_georef
  sicnu_processing
  sicnu_operators
  sicnu_data
  sicnu_jobs
  sicnu_task_center
  GDAL::GDAL
)
if(TARGET jsoncpp)
  target_link_libraries(test_w10_ui_misc_regression PRIVATE jsoncpp)
elseif(JSONCPP_LIBRARIES)
  target_include_directories(test_w10_ui_misc_regression PRIVATE ${JSONCPP_INCLUDE_DIRS})
  target_link_libraries(test_w10_ui_misc_regression PRIVATE ${JSONCPP_LIBRARIES})
else()
  target_link_libraries(test_w10_ui_misc_regression PRIVATE jsoncpp)
endif()
target_include_directories(test_w10_ui_misc_regression PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_w10_ui_misc_regression PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_w10_ui_misc_regression)

add_executable(test_w11_build_parity test_w11_build_parity.cpp)
target_link_libraries(test_w11_build_parity PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
)
target_include_directories(test_w11_build_parity PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
)
target_compile_definitions(test_w11_build_parity PRIVATE
  CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
sicnu_discover_tests(test_w11_build_parity)

add_executable(test_w12_regressions test_w12_regressions.cpp
  ${CMAKE_SOURCE_DIR}/src/app/stac_client.cpp
)
target_link_libraries(test_w12_regressions PRIVATE
  Catch2::Catch2WithMain
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Network
  Qt6::Test
  qgis_core
  qgis_gui
  sicnu_processing
  GDAL::GDAL
)
if(TARGET jsoncpp)
  target_link_libraries(test_w12_regressions PRIVATE jsoncpp)
elseif(JSONCPP_LIBRARIES)
  target_include_directories(test_w12_regressions PRIVATE ${JSONCPP_INCLUDE_DIRS})
  target_link_libraries(test_w12_regressions PRIVATE ${JSONCPP_LIBRARIES})
else()
  target_link_libraries(test_w12_regressions PRIVATE jsoncpp)
endif()
target_include_directories(test_w12_regressions PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_w12_regressions PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_w12_regressions)

add_executable(test_subsystem_stability test_subsystem_stability.cpp
  ${CMAKE_SOURCE_DIR}/src/app/widgets/histogram_widget.cpp
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
  GDAL::GDAL
)
if(TARGET jsoncpp)
  target_link_libraries(test_subsystem_stability PRIVATE jsoncpp)
elseif(JSONCPP_LIBRARIES)
  target_include_directories(test_subsystem_stability PRIVATE ${JSONCPP_INCLUDE_DIRS})
  target_link_libraries(test_subsystem_stability PRIVATE ${JSONCPP_LIBRARIES})
else()
  target_link_libraries(test_subsystem_stability PRIVATE jsoncpp)
endif()
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
  GDAL::GDAL
)
if(TARGET jsoncpp)
  target_link_libraries(test_e2e_phase2 PRIVATE jsoncpp)
elseif(JSONCPP_LIBRARIES)
  target_include_directories(test_e2e_phase2 PRIVATE ${JSONCPP_INCLUDE_DIRS})
  target_link_libraries(test_e2e_phase2 PRIVATE ${JSONCPP_LIBRARIES})
else()
  target_link_libraries(test_e2e_phase2 PRIVATE jsoncpp)
endif()
target_include_directories(test_e2e_phase2 PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${CMAKE_BINARY_DIR}
)
set_target_properties(test_e2e_phase2 PROPERTIES AUTOMOC ON)
sicnu_discover_tests(test_e2e_phase2)
