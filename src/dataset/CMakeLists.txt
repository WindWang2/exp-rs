# Scientific dataset foundation (Foundation 5.0, ADR 0134).
#
# sicnu_dataset owns dataset identity/versioning, samples, annotations, label
# ontology, splits/leakage, quality and catalog contracts. It depends on
# Sicnu::data for the shared Result/Diagnostic vocabulary and the canonical
# JSON serializer — it never re-derives either. Headers stay Qt-only (like
# sicnu_data); SQLite is a PRIVATE detail of the store.

qt_add_library(sicnu_dataset STATIC
  annotation.cpp
  annotation.h
  dataset_ids.cpp
  dataset_ids.h
  dataset_types.cpp
  dataset_types.h
  dataset_manifest.cpp
  dataset_manifest.h
  dataset_fingerprint.cpp
  dataset_fingerprint.h
  dataset_version.cpp
  dataset_version.h
  dataset_quality.cpp
  dataset_quality.h
  fold_audit.cpp
  fold_audit.h
  dataset_store.cpp
  dataset_store.h
  dataset_store_impl.h
  dataset_store_samples.cpp
  dataset_store_splits.cpp
  dataset_store_facets.cpp
  deterministic_random.cpp
  deterministic_random.h
  label_schema.cpp
  label_schema.h
  leakage_audit.cpp
  leakage_audit.h
  patch_generator.cpp
  patch_generator.h
  sample.cpp
  sample.h
  sample_promotion.cpp
  sample_promotion.h
  split.cpp
  split.h
  wkt.cpp
  wkt.h
)

add_library(Sicnu::dataset ALIAS sicnu_dataset)

target_include_directories(sicnu_dataset
  PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(sicnu_dataset
  PUBLIC
    Qt6::Core
    Sicnu::data
  PRIVATE
    SQLite::SQLite3
)

# The dataset layer must stay free of GUI and network dependencies, mirroring
# the sicnu_data layer guard: GUI couples the science core to the shell and
# network I/O belongs to the host.
foreach(SICNU_DATASET_FORBIDDEN_LINK Qt6::Widgets qgis_gui Qt6::Network)
  get_target_property(SICNU_DATASET_DIRECT_LINKS sicnu_dataset LINK_LIBRARIES)
  if(SICNU_DATASET_FORBIDDEN_LINK IN_LIST SICNU_DATASET_DIRECT_LINKS)
    message(FATAL_ERROR
      "sicnu_dataset must not depend on forbidden target ${SICNU_DATASET_FORBIDDEN_LINK}"
    )
  endif()
endforeach()

set_target_properties(sicnu_dataset PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  POSITION_INDEPENDENT_CODE ON
)
