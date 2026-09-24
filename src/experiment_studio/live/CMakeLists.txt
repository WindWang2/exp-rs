# experiment_studio/live — the Studio's live-execution bridge.
#
# A bridge leaf in the spirit of src/study/bridge: it composes the REAL
# providers (StudyRunner + execution spine adapter, GDAL spatial summarizer,
# debugger evidence source + analyzer, faultlab sandbox runner, capsule
# builder/IO/readiness) with the pure projection leaf for the view models.
# The leaf (sicnu_experiment_studio) stays link-clean; this target is the
# opt-in surface (dock, tests) that may touch the execution stack.
#
# Owns NO second store, NO thread pool, NO queue, NO sweep — TaskCenter
# stays the only scheduling authority; run truth stays in ExperimentStore.
qt_add_library(sicnu_experiment_studio_live STATIC
  studio_live.cpp
  studio_live.h
)

add_library(Sicnu::experiment_studio_live ALIAS sicnu_experiment_studio_live)

target_include_directories(sicnu_experiment_studio_live
  PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(sicnu_experiment_studio_live
  PUBLIC
    Qt6::Core
    Sicnu::experiment_studio   # pure projection leaf (VMs, typed errors)
    Sicnu::study               # StudyRunner + GDAL spatial summarizer
    Sicnu::study_bridge        # ExecutionPlaneStudyBackend (TaskCenter spine)
    Sicnu::experiment          # ExperimentStore / MatrixLedger
    Sicnu::experiment_debugger # DirectoryEvidenceSource + divergence analyzer
    Sicnu::faultlab            # real sandbox scenario runner
    Sicnu::experiment_capsule  # CapsuleBuilder / CapsuleIO / CapsuleReadiness
)

set_target_properties(sicnu_experiment_studio_live PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  POSITION_INDEPENDENT_CODE ON
  AUTOMOC OFF
)

# The live bridge still never gains GUI or network behavior — the dock is
# the only widget layer in this story.
foreach(SICNU_STUDIO_LIVE_FORBIDDEN_LINK Qt6::Widgets qgis_gui Qt6::Network Sicnu::app)
  get_target_property(SICNU_STUDIO_LIVE_DIRECT_LINKS sicnu_experiment_studio_live LINK_LIBRARIES)
  if(SICNU_STUDIO_LIVE_FORBIDDEN_LINK IN_LIST SICNU_STUDIO_LIVE_DIRECT_LINKS)
    message(FATAL_ERROR
      "sicnu_experiment_studio_live must not depend on forbidden target ${SICNU_STUDIO_LIVE_FORBIDDEN_LINK}"
    )
  endif()
endforeach()
