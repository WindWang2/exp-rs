###############################################################################
# src/plugins/host/CMakeLists.txt — out-of-process native plugin host
#
#   sicnu_plugins_hostprocess  STATIC (Qt-free): launcher-side session
#                              management, proxies and the registry runtime.
#   exprs_plugin_host_worker   executable: hosts ONE native plugin in its own
#                              process over the versioned IPC contract.
#
# Both link sicnu_sdk (Qt-free); the worker additionally links
# sicnu_operators_core (the plugin link leaf: RSOperator contract + jsoncpp).
###############################################################################

set( EXPRS_PLUGIN_HOSTPROCESS_SOURCES
  plugin_host_session.cpp
  plugin_host_process_runtime.cpp
  plugin_host_proxies.cpp
  plugin_host_operators_shim.cpp
)

add_library( sicnu_plugins_hostprocess STATIC ${EXPRS_PLUGIN_HOSTPROCESS_SOURCES} )

target_include_directories( sicnu_plugins_hostprocess
  PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/sdk
)

target_link_libraries( sicnu_plugins_hostprocess
  PUBLIC
    sicnu_sdk
    sicnu_operators_core
)

target_compile_features( sicnu_plugins_hostprocess PUBLIC cxx_std_20 )
set_target_properties( sicnu_plugins_hostprocess PROPERTIES POSITION_INDEPENDENT_CODE ON )

add_executable( exprs_plugin_host_worker plugin_host_worker_main.cpp )
target_include_directories( exprs_plugin_host_worker
  PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/sdk
)
target_link_libraries( exprs_plugin_host_worker
  PRIVATE
    sicnu_sdk
    sicnu_operators_core
)
set_target_properties( exprs_plugin_host_worker PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
