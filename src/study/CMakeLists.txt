# Parameter Sensitivity & Uncertainty Studio (RS14-07).
# Core study layer: spec, samplers, runner, analysis, export. Depends on the
# experiment authority (store/recorder/matrix) — never on a second store.
# Headers stay Qt Core only; SQLite arrives transitively (PRIVATE).

qt_add_library(sicnu_study STATIC
  study_spec.cpp
  study_spec.h
  study_sampling.cpp
  study_sampling.h
  study_execution.h
  study_runner.cpp
  study_runner.h
  study_analysis.cpp
  study_analysis.h
  study_spatial.cpp
  study_spatial.h
)

add_library(Sicnu::study ALIAS sicnu_study)

target_include_directories(sicnu_study
  PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(sicnu_study
  PUBLIC
    Qt6::Core
    Sicnu::experiment
  PRIVATE
    GDAL::GDAL # raster reading for the spatial difference adapter only
)

# Same science-core layer guard as sicnu_experiment: no GUI, no network.
foreach(SICNU_STUDY_FORBIDDEN_LINK Qt6::Widgets qgis_gui Qt6::Network)
  get_target_property(SICNU_STUDY_DIRECT_LINKS sicnu_study LINK_LIBRARIES)
  if(SICNU_STUDY_FORBIDDEN_LINK IN_LIST SICNU_STUDY_DIRECT_LINKS)
    message(FATAL_ERROR
      "sicnu_study must not depend on forbidden target ${SICNU_STUDY_FORBIDDEN_LINK}"
    )
  endif()
endforeach()

set_target_properties(sicnu_study PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  POSITION_INDEPENDENT_CODE ON
  AUTOMOC OFF
)
