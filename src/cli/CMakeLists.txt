# SICNU GEO RS — Headless CLI pipeline executor
# Requires sicnu_operators (and thus jsoncpp) to already be configured.

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND AND NOT TARGET jsoncpp)
  pkg_check_modules(JSONCPP QUIET jsoncpp)
endif()

add_executable(sicnu_geo_rs_cli
    main_cli.cpp
    cli_commands.cpp
    rs_pipeline_runner.cpp
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
