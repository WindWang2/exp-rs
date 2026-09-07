# src/agent/CMakeLists.txt
add_library(sicnu_agent SHARED
  output_verifier.cpp
  output_verifier.h
  workspace_snapshot.cpp
  workspace_snapshot.h
  workspace_state.cpp
  workspace_state.h
  view_control_service.cpp
  view_control_service.h
  raster_display_service.cpp
  raster_display_service.h
  interaction_tool_registry.cpp
  interaction_tool_registry.h
  agent_copilot_dock_widget.cpp
  agent_copilot_dock_widget.h
  llm_config_manager.cpp
  llm_config_manager.h
  llm_settings_dialog.cpp
  llm_settings_dialog.h
  llm_streaming_client.cpp
  llm_streaming_client.h
  contracts/spatial_contracts.cpp
  contracts/spatial_contracts.h
  harness/harness_error.cpp
  harness/harness_error.h
  harness/tool_taxonomy.cpp
  harness/tool_taxonomy.h
  harness/tool_manifest.cpp
  harness/tool_manifest.h
  harness/harness_tools.cpp
  harness/harness_tools.h
  harness/entity_resolver.cpp
  harness/entity_resolver.h
  harness/grounding_tools.cpp
  harness/grounding_tools.h
  harness/agent_plan.cpp
  harness/agent_plan.h
  harness/scientific_preflight.cpp
  harness/scientific_preflight.h
  harness/harness_verification.cpp
  harness/harness_verification.h
  harness/plan_tools.cpp
  harness/plan_tools.h
  harness/recipe_catalog.cpp
  harness/recipe_catalog.h
  harness/recipe_tools.cpp
  harness/recipe_tools.h
  tool_catalog/agent_tool.cpp
  tool_catalog/agent_tool.h
  tool_catalog/tool_provider.h
  tool_catalog/algorithm_tool_provider.cpp
  tool_catalog/algorithm_tool_provider.h
  tool_catalog/interaction_tool_provider.cpp
  tool_catalog/interaction_tool_provider.h
  tool_catalog/data_tool_provider.cpp
  tool_catalog/data_tool_provider.h
  tool_catalog/agent_tool_catalog.cpp
  tool_catalog/agent_tool_catalog.h
  spatial_tools/governance_tools.cpp
  spatial_tools/governance_tools.h
  spatial_tools/spatial_tool.cpp
  spatial_tools/spatial_tool.h
  spatial_tools/raster_inspect_tool.cpp
  spatial_tools/raster_inspect_tool.h
  spatial_tools/vector_inspect_tool.cpp
  spatial_tools/vector_inspect_tool.h
  spatial_tools/model_catalog_tool.cpp
    spatial_tools/temporal_collection_tools.cpp
    spatial_tools/temporal_workspace_tools.cpp
  spatial_tools/model_catalog_tool.h
  spatial_tools/spatial_tool_provider.cpp
  spatial_tools/spatial_tool_provider.h
  spatial_tools/workspace_tools.cpp
  spatial_tools/workspace_tools.h
  spatial_tools/sample_tools.cpp
  spatial_tools/sample_tools.h
  spatial_tools/result_assessment_tool.cpp
  spatial_tools/result_assessment_tool.h
  spatial_tools/capability_tools.cpp
  spatial_tools/capability_tools.h
  spatial_tools/workflow_preflight_tool.cpp
  spatial_tools/workflow_preflight_tool.h
  cartography/cartography_tools.cpp
  cartography/cartography_tools.h
  cartography/chart_registry.cpp
  cartography/chart_registry.h
  cartography/composition.cpp
  cartography/composition.h
  cartography/design_tokens.cpp
  cartography/design_tokens.h
  cartography/quality.cpp
  cartography/quality.h
  cartography/registry.cpp
  cartography/registry.h
  symbology/symbology_tools.cpp
  symbology/symbology_tools.h
  commands/workspace_commands.cpp
  commands/workspace_commands.h
  mapspec/mapspec.cpp
  mapspec/mapspec.h
  mapspec/mapspec_compiler.cpp
  mapspec/mapspec_compiler.h
  layout_tools/layout_service.cpp
  layout_tools/layout_service.h
  layout_tools/layout_tools.cpp
  layout_tools/layout_tools.h
  layout_tools/layout_tool_provider.cpp
  layout_tools/layout_tool_provider.h
)

target_link_libraries(sicnu_agent PUBLIC
  Qt6::Core
  Qt6::Network
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_data
  sicnu_qgis_display
  sicnu_processing
  sicnu_task_center
  sicnu_operators
  sicnu_workflow
  ${JSONCPP_LIBRARIES}
)

target_include_directories(sicnu_agent
  PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${JSONCPP_INCLUDE_DIRS}
  PRIVATE
    ${CMAKE_SOURCE_DIR}/src/app
)

set_target_properties(sicnu_agent PROPERTIES
  AUTOMOC ON
  WINDOWS_EXPORT_ALL_SYMBOLS ON
)

# Harness 4.0: recipe catalog source-tree fallback (same policy as ModelCatalog).
target_compile_definitions(sicnu_agent PRIVATE
  SICNU_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

install(TARGETS sicnu_agent
  RUNTIME DESTINATION ${QGIS_BIN_DIR}
  LIBRARY DESTINATION ${QGIS_LIB_DIR}
  ARCHIVE DESTINATION ${QGIS_LIB_DIR})
