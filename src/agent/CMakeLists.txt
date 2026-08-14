# src/agent/CMakeLists.txt
add_library(sicnu_agent SHARED
  workspace_snapshot.cpp
  workspace_snapshot.h
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
)

target_include_directories(sicnu_agent PUBLIC
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
)

set_target_properties(sicnu_agent PROPERTIES
  AUTOMOC ON
)
