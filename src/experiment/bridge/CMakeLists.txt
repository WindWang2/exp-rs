# Workflow → Experiment auto-recording adapter (goal 8.0 §A).
#
# A thin leaf target: converts the authoritative WorkflowRunCoordinator
# lifecycle into ExperimentRunBridge events. Deliberately NOT part of
# sicnu_experiment (the science core must not link the execution stack) and
# NOT part of sicnu_workflow (the execution plane must not link the science
# core). Consumers are the opt-in surfaces only (agent, CLI, tests).
qt_add_library(sicnu_experiment_bridge STATIC
  workflow_experiment_adapter.cpp
  workflow_experiment_adapter.h
  execution_event_conversion.cpp
  execution_event_conversion.h
  lab_run_recorder.cpp
  lab_run_recorder.h
  lab_report.cpp
  lab_report.h
  lab_report_writers.cpp
  lab_report_writers.h
)

add_library(Sicnu::experiment_bridge ALIAS sicnu_experiment_bridge)

target_include_directories(sicnu_experiment_bridge
  PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(sicnu_experiment_bridge
  PUBLIC
    Qt6::Core
    Sicnu::experiment
    sicnu_workflow
)

set_target_properties(sicnu_experiment_bridge PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  POSITION_INDEPENDENT_CODE ON
)

# Same science-core guard as sicnu_experiment: the adapter projects execution
# truth into records; it never gains GUI or network behavior.
foreach(SICNU_BRIDGE_FORBIDDEN_LINK Qt6::Widgets qgis_gui Qt6::Network)
  get_target_property(SICNU_BRIDGE_DIRECT_LINKS sicnu_experiment_bridge LINK_LIBRARIES)
  if(SICNU_BRIDGE_FORBIDDEN_LINK IN_LIST SICNU_BRIDGE_DIRECT_LINKS)
    message(FATAL_ERROR
      "sicnu_experiment_bridge must not depend on forbidden target ${SICNU_BRIDGE_FORBIDDEN_LINK}"
    )
  endif()
endforeach()
