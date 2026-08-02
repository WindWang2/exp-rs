# src/agent/CMakeLists.txt
add_library(sicnu_agent SHARED
  workspace_snapshot.cpp
  workspace_snapshot.h
  agent_copilot_dock_widget.cpp
  agent_copilot_dock_widget.h
  llm_config_manager.cpp
  llm_config_manager.h
  llm_settings_dialog.cpp
  llm_settings_dialog.h
  llm_streaming_client.cpp
  llm_streaming_client.h
)

target_link_libraries(sicnu_agent PUBLIC
  Qt6::Core
  Qt6::Network
  Qt6::Widgets
  qgis_core
  qgis_gui
  sicnu_data
  sicnu_processing
  sicnu_task_center
)

target_include_directories(sicnu_agent PUBLIC
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
)
