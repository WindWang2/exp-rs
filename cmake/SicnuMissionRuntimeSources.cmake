# Shared mission-runtime source list (#1186 item 21).
# Included by mission-runtime-gate/CMakeLists.txt and tests/CMakeLists.txt.

set(SICNU_MISSION_RUNTIME_SOURCES_FULL
  ${SICNU_MISSION_SRC_ROOT}/src/app/workbench/mission_stage.cpp
  ${SICNU_MISSION_SRC_ROOT}/src/app/workbench/mission_projection.cpp
  ${SICNU_MISSION_SRC_ROOT}/src/app/workbench/mission_timeline_model.cpp
  ${SICNU_MISSION_SRC_ROOT}/src/app/workbench/mission_timeline_store.cpp
  ${SICNU_MISSION_SRC_ROOT}/src/app/workbench/mission_timeline_bridge.cpp
  ${SICNU_MISSION_SRC_ROOT}/src/app/workbench/mission_runtime_store.cpp
  ${SICNU_MISSION_SRC_ROOT}/src/app/workbench/mission_run_authority.cpp
  ${SICNU_MISSION_SRC_ROOT}/src/app/workbench/mission_context.cpp
  ${SICNU_MISSION_SRC_ROOT}/src/app/workbench/mission_context_store.cpp
  ${SICNU_MISSION_SRC_ROOT}/src/app/workbench/mission_tool_authority.cpp
  ${SICNU_MISSION_SRC_ROOT}/src/app/workbench/object_identity.cpp
  ${SICNU_MISSION_SRC_ROOT}/src/app/workbench/selection_context.cpp
  ${SICNU_MISSION_SRC_ROOT}/src/app/workbench/workbench_host.cpp
  ${SICNU_MISSION_SRC_ROOT}/src/agent/spatial_tools/mission_tools.cpp
  ${SICNU_MISSION_SRC_ROOT}/src/agent/spatial_tools/spatial_tool_registry.cpp
  ${SICNU_MISSION_SRC_ROOT}/src/agent/contracts/spatial_contracts.cpp
  ${SICNU_MISSION_SRC_ROOT}/src/agent/harness/harness_actions.cpp
)

# Gate build stubs object_identity/selection_context/workbench_host.
set(SICNU_MISSION_RUNTIME_SOURCES_GATE
  ${SICNU_MISSION_SRC_ROOT}/src/app/workbench/mission_stage.cpp
  ${SICNU_MISSION_SRC_ROOT}/src/app/workbench/mission_projection.cpp
  ${SICNU_MISSION_SRC_ROOT}/src/app/workbench/mission_timeline_model.cpp
  ${SICNU_MISSION_SRC_ROOT}/src/app/workbench/mission_timeline_store.cpp
  ${SICNU_MISSION_SRC_ROOT}/src/app/workbench/mission_timeline_bridge.cpp
  ${SICNU_MISSION_SRC_ROOT}/src/app/workbench/mission_runtime_store.cpp
  ${SICNU_MISSION_SRC_ROOT}/src/app/workbench/mission_run_authority.cpp
  ${SICNU_MISSION_SRC_ROOT}/src/app/workbench/mission_context.cpp
  ${SICNU_MISSION_SRC_ROOT}/src/app/workbench/mission_context_store.cpp
  ${SICNU_MISSION_SRC_ROOT}/src/app/workbench/mission_tool_authority.cpp
  ${SICNU_MISSION_SRC_ROOT}/src/agent/spatial_tools/mission_tools.cpp
  ${SICNU_MISSION_SRC_ROOT}/src/agent/spatial_tools/spatial_tool_registry.cpp
  ${SICNU_MISSION_SRC_ROOT}/src/agent/contracts/spatial_contracts.cpp
  ${SICNU_MISSION_SRC_ROOT}/src/agent/harness/harness_actions.cpp
)
