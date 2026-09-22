# RS14-01 Scientific Data Passport — GDAL facts collector.
# Qt-free; links GDAL + the jsoncpp-only scientific_state core so the
# collector runs in the io test lane (no Qt, no QGIS).

add_library( sicnu_scientific_state_gdal STATIC
    gdal_state_facts.cpp
)

add_library( Sicnu::ScientificStateGdal ALIAS sicnu_scientific_state_gdal )

target_include_directories( sicnu_scientific_state_gdal PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries( sicnu_scientific_state_gdal PUBLIC
    GDAL::GDAL
    Sicnu::ScientificState
)

set_target_properties( sicnu_scientific_state_gdal PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_STANDARD 17
)
