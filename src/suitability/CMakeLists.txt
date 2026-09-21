# Scene/Dataset Suitability Assessor (RS14).
#
# sicnu_suitability answers "does this dataset / scene set fit this
# experiment goal?" BEFORE any compute is spent. It is a pure science-core
# leaf: value-object DTOs in, versioned report out, no I/O of its own —
# scale queries go through the injected SuitabilityDataProvider. It reuses
# Sicnu::data's Result/Diagnostic vocabulary and Sicnu::dataset's
# BenchmarkTaskFamily; it never re-derives either.

qt_add_library(sicnu_suitability STATIC
  criteria_spatial.cpp
  criteria_spatial.h
  criteria_spectral.cpp
  criteria_spectral.h
  criteria_temporal.cpp
  criteria_temporal.h
  dataset_facts.cpp
  dataset_facts.h
  scene_candidate.cpp
  scene_candidate.h
  seasonality.cpp
  seasonality.h
  suitability_assessor.cpp
  suitability_assessor.h
  suitability_goal.cpp
  suitability_goal.h
  suitability_level.cpp
  suitability_level.h
  suitability_types.cpp
  suitability_types.h
  suitability_report.cpp
  suitability_report.h
)

add_library(Sicnu::suitability ALIAS sicnu_suitability)

target_include_directories(sicnu_suitability
  PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(sicnu_suitability
  PUBLIC
    Qt6::Core
    Sicnu::data
    Sicnu::dataset
)

# Same science-core layer guard as sicnu_dataset: no GUI, no network.
foreach(SICNU_SUITABILITY_FORBIDDEN_LINK Qt6::Widgets qgis_gui Qt6::Network)
  get_target_property(SICNU_SUITABILITY_DIRECT_LINKS sicnu_suitability LINK_LIBRARIES)
  if(SICNU_SUITABILITY_FORBIDDEN_LINK IN_LIST SICNU_SUITABILITY_DIRECT_LINKS)
    message(FATAL_ERROR
      "sicnu_suitability must not depend on forbidden target ${SICNU_SUITABILITY_FORBIDDEN_LINK}"
    )
  endif()
endforeach()

set_target_properties(sicnu_suitability PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  POSITION_INDEPENDENT_CODE ON
)
