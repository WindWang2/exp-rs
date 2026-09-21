# RS14-01 Scientific Data Passport — core value types, resolver, JSON, diff.
# Deliberately Qt-free and GDAL-free: jsoncpp only, so the core contracts run
# in the lightest test lane (sicnu_add_sdk_test) without GUI/QGIS linking.

add_library( sicnu_scientific_state STATIC
    asset_state_json.cpp
)

add_library( Sicnu::ScientificState ALIAS sicnu_scientific_state )

target_include_directories( sicnu_scientific_state PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

# jsoncpp: the canonical `jsoncpp` target is resolved once at the top level
# (root CMakeLists.txt) — mirror the fallback pattern used by sicnu_geospatial.
if ( TARGET jsoncpp )
    target_link_libraries( sicnu_scientific_state PUBLIC jsoncpp )
elseif ( JSONCPP_LIBRARIES )
    target_include_directories( sicnu_scientific_state SYSTEM PUBLIC ${JSONCPP_INCLUDE_DIRS} )
    target_link_libraries( sicnu_scientific_state PUBLIC ${JSONCPP_LIBRARIES} )
else()
    target_link_libraries( sicnu_scientific_state PUBLIC jsoncpp )
endif()

set_target_properties( sicnu_scientific_state PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_STANDARD 17
)
