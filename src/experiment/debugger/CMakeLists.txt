qt_add_library(sicnu_experiment_debugger STATIC
  run_snapshot.cpp
  run_snapshot.h
  evidence_source.cpp
  evidence_source.h
  snapshot_builder.cpp
  snapshot_builder.h
  step_aligner.cpp
  step_aligner.h
  first_divergence.cpp
  first_divergence.h
  equivalence.cpp
  equivalence.h
  artifact_metrics.cpp
  artifact_metrics.h
  timeline_model.cpp
  timeline_model.h
  agent_diagnostic.cpp
  agent_diagnostic.h
  debugger_types.h
)

add_library(Sicnu::experiment_debugger ALIAS sicnu_experiment_debugger)

target_include_directories(sicnu_experiment_debugger
  PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

# Layer contract (mirrors sicnu_experiment / sicnu_experiment_bridge):
# the debugger is a read-only science-side projection. It links the science
# core and the workflow core for their READ surfaces only (strict checkpoint
# loader + provenance graph parser); nothing links into it except opt-in
# consumers, and the science core never links back.
target_link_libraries(sicnu_experiment_debugger
  PUBLIC
    Qt6::Core
    Sicnu::dataset
    Sicnu::experiment
    sicnu_workflow
)

set_target_properties(sicnu_experiment_debugger PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  POSITION_INDEPENDENT_CODE ON
)

# Same layer guard as sicnu_experiment: the debugger must stay GUI-free and
# network-free — it is a read-only projection, never an interactive surface.
foreach(SICNU_DEBUGGER_FORBIDDEN_LINK Qt6::Widgets qgis_gui Qt6::Network)
  get_target_property(SICNU_DEBUGGER_DIRECT_LINKS sicnu_experiment_debugger LINK_LIBRARIES)
  if(SICNU_DEBUGGER_FORBIDDEN_LINK IN_LIST SICNU_DEBUGGER_DIRECT_LINKS)
    message(FATAL_ERROR
      "sicnu_experiment_debugger must not depend on forbidden target ${SICNU_DEBUGGER_FORBIDDEN_LINK}"
    )
  endif()
endforeach()
