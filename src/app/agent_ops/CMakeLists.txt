# src/app/agent_ops — Control Center UI + Qt ExperimentStore writer
#
# Thin Qt surface over sicnu_agent_ops. Does not own loop/benchmark science.

qt_add_library(sicnu_agent_ops_ui STATIC
  agent_ops_control_center_panel.cpp
  agent_ops_control_center_panel.h
  benchmark_store_writer.cpp
  benchmark_store_writer.h
)

add_library(Sicnu::agent_ops_ui ALIAS sicnu_agent_ops_ui)

target_include_directories(sicnu_agent_ops_ui
  PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/app
    ${CMAKE_BINARY_DIR}
)

target_link_libraries(sicnu_agent_ops_ui
  PUBLIC
    Qt6::Core
    Qt6::Widgets
    sicnu_agent_ops
    sicnu_experiment
)

set_target_properties(sicnu_agent_ops_ui PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  POSITION_INDEPENDENT_CODE ON
  AUTOMOC ON
)
