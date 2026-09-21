# SICNU GEO RS — Headless CLI pipeline executor
# Requires sicnu_operators (and thus jsoncpp) to already be configured.

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND AND NOT TARGET jsoncpp)
  pkg_check_modules(JSONCPP QUIET jsoncpp)
endif()

add_executable(sicnu_geo_rs_cli
    main_cli.cpp
    cli_commands.cpp
    cli_env_doctor.cpp
    cli_tool_commands.cpp
    cli_tool_commands.h
    cli_passport_commands.cpp
    cli_passport_commands.h
    cli_dataset_commands.cpp
    cli_lab_commands.cpp
    cli_project_ops.cpp
    rs_pipeline_runner.cpp
    help_cli_projections.cpp
    # Unified Help 6.0: embedded knowledge content (RCC init in the exe).
    ${CMAKE_SOURCE_DIR}/data/help/help_content.qrc
)

# D7 batch grading engine: a small static lib so test_lab_batch can drive the
# runner without linking the whole CLI executable.
add_library(sicnu_lab_batch STATIC
    lab_batch_runner.cpp
    lab_batch_runner.h
)
target_include_directories(sicnu_lab_batch PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
)

# Surface-11 batch manifest runner: a small static lib so
# test_cli_batch_manifest can drive the runner without the CLI executable.
add_library(sicnu_cli_batch STATIC
    cli_batch_runner.cpp
    cli_batch_runner.h
)
target_include_directories(sicnu_cli_batch PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
)
target_link_libraries(sicnu_cli_batch PUBLIC
    Qt6::Core
    sicnu_agent
    sicnu_operators
    sicnu_processing
)
if(TARGET jsoncpp)
    target_link_libraries(sicnu_cli_batch PUBLIC jsoncpp)
elseif(JSONCPP_LIBRARIES)
    target_link_libraries(sicnu_cli_batch PUBLIC ${JSONCPP_LIBRARIES})
endif()
target_link_libraries(sicnu_lab_batch PUBLIC
    Qt6::Core
    sicnu_agent
    sicnu_sdk
)
if(TARGET jsoncpp)
    target_link_libraries(sicnu_lab_batch PUBLIC jsoncpp)
elseif(JSONCPP_LIBRARIES)
    target_link_libraries(sicnu_lab_batch PUBLIC ${JSONCPP_LIBRARIES})
endif()

# teaching-lab-platform-11: headless sicnu.labreport.v1 export (`lab --report`)
# — a static lib so tests drive the runner without linking the whole CLI.
add_library(sicnu_lab_report STATIC
    lab_report_runner.cpp
    lab_report_runner.h
)
target_include_directories(sicnu_lab_report PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
)
target_link_libraries(sicnu_lab_report PUBLIC
    Qt6::Core
    sicnu_sdk
    Sicnu::experiment
    Sicnu::experiment_bridge
)
if(TARGET jsoncpp)
    target_link_libraries(sicnu_lab_report PUBLIC jsoncpp)
elseif(JSONCPP_LIBRARIES)
    target_link_libraries(sicnu_lab_report PUBLIC ${JSONCPP_LIBRARIES})
endif()

# teaching-lab-platform-11: `lab --self-check` environment diagnostics.
add_library(sicnu_lab_self_check STATIC
    lab_self_check.cpp
    lab_self_check.h
)
target_include_directories(sicnu_lab_self_check PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
)
target_compile_definitions(sicnu_lab_self_check PUBLIC
    SICNU_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
target_link_libraries(sicnu_lab_self_check PUBLIC
    Qt6::Core
    Sicnu::data
    sicnu_agent
    GDAL::GDAL
)
if(TARGET jsoncpp)
    target_link_libraries(sicnu_lab_self_check PUBLIC jsoncpp)
elseif(JSONCPP_LIBRARIES)
    target_link_libraries(sicnu_lab_self_check PUBLIC ${JSONCPP_LIBRARIES})
endif()

# Headless CLI embeds no QML; skip Qt's configure-time qmlimportscanner pass
# (unreliable on Windows hosts — see the tail of tests/CMakeLists.txt).
set_target_properties(sicnu_geo_rs_cli PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN TRUE)

# Unified Help 6.0 projections (--operator-help / --help-topic /
# --list-topics / --export-help-docs).
target_link_libraries(sicnu_geo_rs_cli PRIVATE
    Sicnu::ScientificStateGdal
    Sicnu::help
    Sicnu::help_adapters
    # cli_dataset_commands.cpp (Foundation 5.0) calls the dataset/experiment
    # stores; the CLI previously never linked them (latent master defect the
    # full-tree link surfaced).
    Sicnu::dataset
    Sicnu::experiment
)

# Headless plugin hosting (ADR 0023): the runner composes PluginHost with a
# view-less ProjectContext and a widget-free SicnuAppInterface. Compiled in
# only when embedded-Python support is enabled, mirroring the
# SICNU_EMBED_PYTHON guards in plugin_host.cpp / rs_pipeline_runner.cpp.
if(SICNU_EMBED_PYTHON)
    target_sources(sicnu_geo_rs_cli PRIVATE
        ${CMAKE_SOURCE_DIR}/src/core/plugin_host.cpp
        ${CMAKE_SOURCE_DIR}/src/app/python/sicnu_app_interface.cpp
    )
    target_include_directories(sicnu_geo_rs_cli PRIVATE
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/app
    )
    target_link_libraries(sicnu_geo_rs_cli PRIVATE
        Qt6::Gui
        Qt6::Widgets
        qgis_gui
    )
    target_compile_definitions(sicnu_geo_rs_cli PRIVATE SICNU_EMBED_PYTHON=1)
endif()

target_include_directories(sicnu_geo_rs_cli PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
)

target_link_libraries(sicnu_geo_rs_cli PRIVATE
    Qt6::Core
    Sicnu::data
    sicnu_lab_batch
    sicnu_cli_batch
    sicnu_lab_report
    sicnu_lab_self_check
    Sicnu::dataset
    Sicnu::experiment
    Sicnu::experiment_bridge
    SQLite::SQLite3
    Sicnu::Geospatial
    sicnu_plugins_framework
    sicnu_sdk
    sicnu_python_isolated
    Sicnu::qgis_display
    sicnu_operators
    sicnu_processing
    sicnu_task_center
    sicnu_jobs
    sicnu_workflow
    sicnu_workflow_runtime
    GDAL::GDAL
)

# Prefer imported/target form used by the app; fall back to pkg-config vars.
if(TARGET jsoncpp)
    target_link_libraries(sicnu_geo_rs_cli PRIVATE jsoncpp)
elseif(JSONCPP_LIBRARIES)
    target_include_directories(sicnu_geo_rs_cli PRIVATE ${JSONCPP_INCLUDE_DIRS})
    target_link_libraries(sicnu_geo_rs_cli PRIVATE ${JSONCPP_LIBRARIES})
    if(JSONCPP_CFLAGS_OTHER)
        target_compile_options(sicnu_geo_rs_cli PRIVATE ${JSONCPP_CFLAGS_OTHER})
    endif()
else()
    # Last resort: link by library name
    target_link_libraries(sicnu_geo_rs_cli PRIVATE jsoncpp)
endif()

set_target_properties(sicnu_geo_rs_cli PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    OUTPUT_NAME "sicnu_geo_rs_cli"
)

install(TARGETS sicnu_geo_rs_cli
    RUNTIME DESTINATION bin
)

# Isolated local worker (Data Plane 3.0, Phase K): a minimal operator host
# speaking worker_protocol v1 over stdin/stdout. Crash in here never takes a
# host application down — that is the isolation boundary.
add_executable(sicnu_worker
    sicnu_worker_main.cpp
)
target_include_directories(sicnu_worker PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
)
target_link_libraries(sicnu_worker PRIVATE
    Qt6::Core
    Sicnu::data
    sicnu_operators
    GDAL::GDAL
)
set_target_properties(sicnu_worker PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN TRUE)
if(TARGET jsoncpp)
    target_link_libraries(sicnu_worker PRIVATE jsoncpp)
elseif(JSONCPP_LIBRARIES)
    target_include_directories(sicnu_worker PRIVATE ${JSONCPP_INCLUDE_DIRS})
    target_link_libraries(sicnu_worker PRIVATE ${JSONCPP_LIBRARIES})
endif()
set_target_properties(sicnu_worker PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    OUTPUT_NAME "sicnu_worker"
)
