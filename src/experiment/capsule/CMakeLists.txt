# RS14-17: ReproducibilityCapsule — a versioned, canonicalized, portable
# PROJECTION of recorded experiment truth (run + dataset pins + evidence +
# lineage). Layers on the existing reproduction-bundle facts; never a second
# store. Headers stay Qt-only; no GUI, no network, no src/agent dependency
# (capability/verifier facts arrive through injected CapsuleHooks).
qt_add_library(sicnu_experiment_capsule STATIC
  capsule_document.cpp
  capsule_document.h
)

add_library(Sicnu::experiment_capsule ALIAS sicnu_experiment_capsule)

target_include_directories(sicnu_experiment_capsule
  PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(sicnu_experiment_capsule
  PUBLIC
    Qt6::Core
    Sicnu::experiment
)
