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