###############################################################################
# src/plugins/framework/CMakeLists.txt — plugin host runtime (Phase D–S bridge)
#
# Target `sicnu_plugins_framework` (STATIC, Qt Core + Widgets for the UI
# host): bridges the sicnu_sdk plugin infrastructure into the application
# registries (RSOperatorRegistry, AtomicAlgorithmRegistry, ModelRuntimeRegistry,
# AgentToolCatalog, DataProviderRegistry).
###############################################################################

set( EXPRS_FRAMEWORK_SOURCES
  plugin_operator_adapter.cpp
  external_tool_operator.cpp
  data_provider_registry.cpp
  plugin_agent_tool_provider.cpp
  plugin_model_runtime_bridge.cpp
  plugin_runtime_host.cpp
  plugin_ui_host.cpp
)

add_library( sicnu_plugins_framework STATIC ${EXPRS_FRAMEWORK_SOURCES} )

target_include_directories( sicnu_plugins_framework
  PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/sdk
)

target_link_libraries( sicnu_plugins_framework
  PUBLIC
    sicnu_sdk
    sicnu_operators
    sicnu_processing
    sicnu_agent
    jsoncpp
    Qt6::Core
    Qt6::Widgets
)

target_compile_features( sicnu_plugins_framework PUBLIC cxx_std_20 )
set_target_properties( sicnu_plugins_framework PROPERTIES AUTOMOC ON POSITION_INDEPENDENT_CODE ON )
