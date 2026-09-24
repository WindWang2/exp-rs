# src/agent_ops — Agent Operations & Recovery Control Center
#
# Compose layer over agent_loop / agentbench / autonomy / repair_planner.
# Plain C++20 + jsoncpp; Qt-free so Control Center logic and closed-loop
# tests run headless. Qt ExperimentStore writer + UI live under
# src/app/agent_ops/.

add_library(sicnu_agent_ops STATIC
  ops_types.cpp
  secret_redactor.cpp
  live_session_recorder.cpp
  diagnostic_bridge.cpp
  autonomy_ops_gate.cpp
  repair_approval.cpp
  recovery_bridge.cpp
  delivery_assembler.cpp
  benchmark_adapter.cpp
  ops_projection.cpp
  resume_reconciler.cpp
  operations_coordinator.cpp
  session_surface.cpp
)

add_library(Sicnu::agent_ops ALIAS sicnu_agent_ops)

target_include_directories(sicnu_agent_ops PUBLIC ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(sicnu_agent_ops PUBLIC
  sicnu_agent_loop
  sicnu_agentbench
  sicnu_autonomy
  sicnu_repair_planner
)

set_target_properties(sicnu_agent_ops PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  POSITION_INDEPENDENT_CODE ON
)
