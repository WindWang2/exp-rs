qt_add_library(sicnu_data STATIC
  asset_types.cpp
  asset_types.h
  collection_types.cpp
  collection_types.h
  data_asset.cpp
  data_asset.h
  data_manager.cpp
  data_manager.h
  data_result.h
  derivation_record.cpp
  derivation_record.h
  internal/source_provider.h
  internal/source_provider_registry.cpp
  internal/source_provider_registry.h
  providers/gdal_runtime.cpp
  providers/gdal_runtime.h
  providers/gdal_raster_source_provider.cpp
  providers/gdal_raster_source_provider.h
  providers/ogr_vector_source_provider.cpp
  providers/ogr_vector_source_provider.h
  providers/remote_map_provider_common.cpp
  providers/remote_map_provider_common.h
  providers/tms_source_provider.cpp
  providers/tms_source_provider.h
  providers/virtual_raster_source_provider.cpp
  providers/virtual_raster_source_provider.h
  providers/wms_source_provider.cpp
  providers/wms_source_provider.h
  providers/wmts_source_provider.cpp
  providers/wmts_source_provider.h
  providers/xyz_source_provider.cpp
  providers/xyz_source_provider.h
  processing_asset_resolver.cpp
  processing_asset_resolver.h
  source_descriptor.cpp
  source_descriptor.h
  virtual_raster_preflight.cpp
  virtual_raster_preflight.h
  virtual_raster_recipe.cpp
  virtual_raster_recipe.h
)

add_library(Sicnu::data ALIAS sicnu_data)

target_include_directories(sicnu_data
  PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(sicnu_data
  PUBLIC
    Qt6::Core
    GDAL::GDAL
)

get_target_property(SICNU_DATA_DIRECT_LINKS sicnu_data LINK_LIBRARIES)
# src/data must stay free of GUI AND network dependencies: GUI couples the data
# layer to the shell, and network I/O belongs to the host (src/app) so the data
# layer's remote-map providers probe through an injected NetworkProbe rather
# than touching Qt Network directly. Guard both here so a regression fails the
# build rather than silently widening the layer's dependency surface.
foreach(SICNU_DATA_FORBIDDEN_LINK Qt6::Widgets qgis_gui Qt6::Network)
  if(SICNU_DATA_FORBIDDEN_LINK IN_LIST SICNU_DATA_DIRECT_LINKS)
    message(FATAL_ERROR
      "sicnu_data must not depend on forbidden target ${SICNU_DATA_FORBIDDEN_LINK}"
    )
  endif()
endforeach()

set_target_properties(sicnu_data PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
)
