qt_add_library(sicnu_data STATIC
  asset_types.cpp
  asset_types.h
  data_asset.h
  data_manager.cpp
  data_manager.h
  data_result.h
  internal/source_provider.h
  internal/source_provider_registry.cpp
  internal/source_provider_registry.h
  providers/gdal_runtime.cpp
  providers/gdal_runtime.h
  providers/gdal_raster_source_provider.cpp
  providers/gdal_raster_source_provider.h
  providers/ogr_vector_source_provider.cpp
  providers/ogr_vector_source_provider.h
  source_descriptor.cpp
  source_descriptor.h
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
foreach(SICNU_DATA_FORBIDDEN_LINK Qt6::Widgets qgis_gui)
  if(SICNU_DATA_FORBIDDEN_LINK IN_LIST SICNU_DATA_DIRECT_LINKS)
    message(FATAL_ERROR
      "sicnu_data must not depend on GUI target ${SICNU_DATA_FORBIDDEN_LINK}"
    )
  endif()
endforeach()

set_target_properties(sicnu_data PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
)
