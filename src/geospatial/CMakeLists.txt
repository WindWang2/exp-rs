# SICNU GEO RS — Geospatial I/O Foundation 4.0
# Qt-free core: canonical metadata, CRS policy, raster/vector I/O contracts,
# certified format profiles, COG validation/presets, STAC mapping, multidim
# views, product adapters, data doctor, atomic FS primitives.
# Deliberately depends only on: C++20 std, GDAL (incl. OSR/PROJ), jsoncpp.

set(SICNU_GEOSPATIAL_SOURCES
    metadata/canonical_metadata.cpp
    crs/crs_policy.cpp
    crs/grid_descriptor.cpp
    identity/asset_identity.cpp
    catalog/asset_query.cpp
    hints/data_locality.cpp
    raster/raster_reader.cpp
    raster/raster_writer.cpp
    vector/vector_reader.cpp
    vector/vector_writer.cpp
    util/atomic_fs.cpp
    util/resource_uri.cpp
    util/time_normalization.cpp
    util/sha256.cpp
    remote/offline_gate.cpp
    remote/offline_gate.h
    remote/remote_identity_token.cpp
    probe/probe.cpp
    remote/probe_remote.cpp
    remote/http_fetch.cpp
    remote/remote_source_validator.cpp
    remote/remote_identity_resolver.cpp
    remote/range_cache_disk.cpp
    remote/range_cache.cpp
    formats/format_profiles.cpp
    cog/cog_presets.cpp
    cog/cog_validator.cpp
    convert/raster_convert.cpp
    stac/stac_mapper.cpp
    stac/stac_client.cpp
    products/product_adapters.cpp
    products/product_registry.cpp
    products/sensor_profile.cpp
    products/cn_product_metadata.cpp
    products/cn_product_adapters.cpp
    doctor/data_doctor.cpp
    multidim/multidim_view.cpp
    multidim/multidim_cube.cpp
    # Cloud-Native Data Fabric / Data Cube 10.0
    fabric/object_store.cpp
    fabric/catalog_service.cpp
    fabric/virtual_cube.cpp
    fabric/chunk_plan.cpp
    fabric/mirror.cpp
    fabric/query_planner.cpp
    fabric/prefetch.cpp
    # Geospatial I/O, COG & Interchange 11.0
    io/param_guard.cpp
    io/finalize_manifest.cpp
    io/stage_ledger.cpp
    io/cog_options.cpp
    io/vector_interchange.cpp
    io/subdataset_inventory.cpp
    io/metadata_patch.cpp
    io/gdal_feature_probe.cpp
)

qt_add_library(sicnu_geospatial STATIC ${SICNU_GEOSPATIAL_SOURCES})
add_library(Sicnu::Geospatial ALIAS sicnu_geospatial)

target_include_directories(sicnu_geospatial PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(sicnu_geospatial PUBLIC
    GDAL::GDAL
)

# Product adapters parse Sentinel/Landsat XML with expat (already a global
# dependency; GDAL does not re-export its own copy).
find_package(EXPAT REQUIRED)
target_link_libraries(sicnu_geospatial PUBLIC EXPAT::EXPAT)

# jsoncpp: the canonical `jsoncpp` target is resolved once at the top level
# (root CMakeLists.txt) — mirror the fallback pattern used by sicnu_processing.
if(TARGET jsoncpp)
    target_link_libraries(sicnu_geospatial PUBLIC jsoncpp)
elseif(JSONCPP_LIBRARIES)
    target_include_directories(sicnu_geospatial SYSTEM PUBLIC ${JSONCPP_INCLUDE_DIRS})
    target_link_libraries(sicnu_geospatial PUBLIC ${JSONCPP_LIBRARIES})
else()
    target_link_libraries(sicnu_geospatial PUBLIC jsoncpp)
endif()

# Leaf static lib linked into shared targets; keep it PIC like sicnu_operators_core.
set_target_properties(sicnu_geospatial PROPERTIES POSITION_INDEPENDENT_CODE ON)
