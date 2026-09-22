# Scientific experiment & reproducibility foundation (Foundation 5.0,
# ADR 0137/0138). Depends on Sicnu::dataset (which carries Sicnu::data for
# Result/Diagnostic + canonical JSON). Headers stay Qt-only; SQLite PRIVATE.

qt_add_library(sicnu_experiment STATIC
  evaluation.cpp
  evaluation.h
  experiment_ids.cpp
  experiment_ids.h
  experiment_types.cpp
  experiment_types.h
  experiment_store.cpp
  experiment_store.h
  run_recorder.cpp
  run_bridge.cpp
  run_recorder.h
  run_bridge.h
  evidence.cpp
  evidence.h
  experiment_matrix.cpp
  experiment_matrix.h
  metric_path.h
  promotion.cpp
  promotion.h
  replay_readiness.cpp
  replay_deviation.cpp
  replay_deviation.h
  repeat_execution.cpp
  repeat_execution.h
  replay_readiness.h
  comparison_ext.cpp
  comparison_ext.h
  lineage.cpp
  lineage.h
  reproduction_bundle.cpp
  reproduction_bundle.h
  reproduction_bundle_import.cpp
  reproduction_bundle_import.h
  benchmark_definition.cpp
  benchmark_definition.h
  benchmark_runner.cpp
  benchmark_runner.h
  benchmark_compare.cpp
  benchmark_compare.h
  benchmark_service.cpp
  benchmark_service.h
)

add_library(Sicnu::experiment ALIAS sicnu_experiment)

target_include_directories(sicnu_experiment
  PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(sicnu_experiment
  PUBLIC
    Qt6::Core
    Sicnu::dataset
  PRIVATE
    SQLite::SQLite3
    # Unified trace + fault registry (Verification Platform 8.0): the store
    # commit seams emit exp.trace.v1 records and probe SICNU_FAULT_POINT.
    # PRIVATE keeps the science-core layer guards (no GUI/network) intact.
    sicnu_runtime
)

# Same layer guard as sicnu_dataset: no GUI, no network in the science core.
foreach(SICNU_EXPERIMENT_FORBIDDEN_LINK Qt6::Widgets qgis_gui Qt6::Network)
  get_target_property(SICNU_EXPERIMENT_DIRECT_LINKS sicnu_experiment LINK_LIBRARIES)
  if(SICNU_EXPERIMENT_FORBIDDEN_LINK IN_LIST SICNU_EXPERIMENT_DIRECT_LINKS)
    message(FATAL_ERROR
      "sicnu_experiment must not depend on forbidden target ${SICNU_EXPERIMENT_FORBIDDEN_LINK}"
    )
  endif()
endforeach()

set_target_properties(sicnu_experiment PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  POSITION_INDEPENDENT_CODE ON
)

# RS14-17 reproducibility capsule: a projection module ON TOP of this
# library's recorded facts (never a second store). Kept as a separate
# target so the heavy experiment sources are not relinked when only the
# capsule changes.
add_subdirectory(capsule)
