# sicnu_platform — the bottom-layer durability authority (Qt-free, GDAL-free,
# dependency-free beyond portable.h). Home of the single sidecar write
# authority (durable_sidecar.h) — every sidecar-class durable write converges
# here; dataset GROUP publishing stays in geo::atomic_fs. See
# durable_sidecar.h for the two-layer contract.
add_library(sicnu_platform STATIC durable_sidecar.cpp)
add_library(Sicnu::Platform ALIAS sicnu_platform)
target_include_directories(sicnu_platform PUBLIC
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
  $<INSTALL_INTERFACE:include>
)
# Consumers of the write authority include shared libraries (sicnu_runtime,
# qgis_analysis, sicnu_workflow, ...). Without PIC on this static archive the
# thread_local RNG in claimStagedPath() produces a TLS-offset relocation the
# linker refuses when placing it into a shared object.
set_target_properties(sicnu_platform PROPERTIES POSITION_INDEPENDENT_CODE ON)

# sicnu_sdk carries Sicnu::Platform in its installed export set; the
# authority must be exported alongside it or the SDK package config
# references a target that does not exist.
install( TARGETS sicnu_platform
  EXPORT ExpRSTargets
  ARCHIVE DESTINATION lib COMPONENT sdk
  LIBRARY DESTINATION lib COMPONENT sdk
  INCLUDES DESTINATION include
)


# The exported target must ship its headers (the SDK package links
# ExpRS::sicnu_platform; without these the installed target is unbuildable).
install( FILES durable_sidecar.h portable.h
  DESTINATION include/platform
  COMPONENT sdk
)
