# src/verify_adapters — REAL providers for the ADR 0172 verifier seam
# (verify_context.h): filesystem artifact probe, exp-rs-prov/1 +
# d17_provenance provenance view, d17_pipeline_checkpoint state view, and
# the GDAL-backed grid probe.
#
# These ADAPT existing authorities onto the verifier's provider interfaces;
# they judge nothing themselves (no verdict logic, no metrics — that is the
# engine's exclusive role) and they must never become a second verifier.
# Two libraries so consumers that need no GDAL can stay light:
#
#   sicnu_verify_adapters       — pure C++20 + jsoncpp + sicnu_verifier
#   sicnu_verify_adapters_gdal  — adds the grid probe over Sicnu::Geospatial
#
# The verifier leaf itself keeps its zero-dependency discipline: these
# adapters live OUTSIDE src/verify by contract (see src/verify/CMakeLists.txt).

add_library( sicnu_verify_adapters STATIC
    bounded_io.cpp
    fs_artifact_probe.cpp
    provenance_sidecar_view.cpp
    checkpoint_state_view.cpp
)

target_include_directories( sicnu_verify_adapters PUBLIC ${CMAKE_SOURCE_DIR}/src )

set_target_properties( sicnu_verify_adapters PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

# jsoncpp arrives transitively through sicnu_verifier's PUBLIC link; the
# adapter layer links the leaf, never around it.
target_link_libraries( sicnu_verify_adapters PUBLIC sicnu_verifier )

# Layer guard — LAST so it observes every link declared above: the adapter
# layer stays Qt/GDAL-free and never reaches around the engine's provider
# seam (grid facts are the gdal variant's business; verdicts are the
# engine's). Tripwire scope: this snapshot is configure-time; a later
# cross-directory target_link_libraries would bypass it, so keep links in
# this file.
get_target_property( SICNU_VERIFY_ADAPTERS_LINK_LIBRARIES sicnu_verify_adapters LINK_LIBRARIES )
set( SICNU_VERIFY_ADAPTERS_ALLOWED_LINKS jsoncpp jsoncpp_lib sicnu_verifier )
foreach( SICNU_VERIFY_ADAPTERS_LINK IN LISTS SICNU_VERIFY_ADAPTERS_LINK_LIBRARIES )
    if ( NOT SICNU_VERIFY_ADAPTERS_LINK IN_LIST SICNU_VERIFY_ADAPTERS_ALLOWED_LINKS )
        message( FATAL_ERROR
            "sicnu_verify_adapters must link jsoncpp + sicnu_verifier only — found '${SICNU_VERIFY_ADAPTERS_LINK}'" )
    endif()
endforeach()

# GDAL-backed grid probe: same seam, one more authority (the geospatial I/O
# foundation — RasterReader, bandSentinelMatches, ensureGdalRegistered).
add_library( sicnu_verify_adapters_gdal STATIC
    gdal_grid_probe.cpp
)

target_include_directories( sicnu_verify_adapters_gdal PUBLIC ${CMAKE_SOURCE_DIR}/src )

set_target_properties( sicnu_verify_adapters_gdal PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

target_link_libraries( sicnu_verify_adapters_gdal PUBLIC
    sicnu_verify_adapters
    Sicnu::Geospatial
)

# Layer guard: the gdal variant adds ONLY the geospatial foundation — no
# Qt, no QGIS, no app/processing (a grid probe is not a UI or a pipeline).
get_target_property( SICNU_VERIFY_ADAPTERS_GDAL_LINK_LIBRARIES sicnu_verify_adapters_gdal LINK_LIBRARIES )
set( SICNU_VERIFY_ADAPTERS_GDAL_ALLOWED_LINKS sicnu_verify_adapters Sicnu::Geospatial )
foreach( SICNU_VERIFY_ADAPTERS_GDAL_LINK IN LISTS SICNU_VERIFY_ADAPTERS_GDAL_LINK_LIBRARIES )
    if ( NOT SICNU_VERIFY_ADAPTERS_GDAL_LINK IN_LIST SICNU_VERIFY_ADAPTERS_GDAL_ALLOWED_LINKS )
        message( FATAL_ERROR
            "sicnu_verify_adapters_gdal must link only sicnu_verify_adapters + Sicnu::Geospatial — found '${SICNU_VERIFY_ADAPTERS_GDAL_LINK}'" )
    endif()
endforeach()
