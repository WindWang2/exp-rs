# Experiment Exploration Studio — projection leaf (Qt Core only).
#
# Projects study / faultlab / debugger / verifier evidence into machine-readable
# view models. Owns NO second sweep, NO thread pool, NO silent resample.

qt_add_library(sicnu_experiment_studio STATIC
  studio_types.h
  studio_errors.cpp
  studio_errors.h
  study_designer.cpp
  study_designer.h
  run_matrix_projection.cpp
  run_matrix_projection.h
  spatial_compare_projection.cpp
  spatial_compare_projection.h
  sensitivity_projection.cpp
  sensitivity_projection.h
  fault_teaching_projection.cpp
  fault_teaching_projection.h
  first_divergence_projection.cpp
  first_divergence_projection.h
  studio_export.cpp
  studio_export.h
  studio_session.cpp
  studio_session.h
)

add_library(Sicnu::experiment_studio ALIAS sicnu_experiment_studio)

target_include_directories(sicnu_experiment_studio
  PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(sicnu_experiment_studio
  PUBLIC
    Qt6::Core
    Sicnu::study
)

foreach(SICNU_STUDIO_FORBIDDEN_LINK Qt6::Widgets qgis_gui Qt6::Network Sicnu::app)
  get_target_property(SICNU_STUDIO_DIRECT_LINKS sicnu_experiment_studio LINK_LIBRARIES)
  if(SICNU_STUDIO_FORBIDDEN_LINK IN_LIST SICNU_STUDIO_DIRECT_LINKS)
    message(FATAL_ERROR
      "sicnu_experiment_studio must not depend on forbidden target ${SICNU_STUDIO_FORBIDDEN_LINK}"
    )
  endif()
endforeach()

set_target_properties(sicnu_experiment_studio PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  POSITION_INDEPENDENT_CODE ON
  AUTOMOC OFF
)
