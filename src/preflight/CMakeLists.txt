# src/preflight — Scientific Preflight Engine (RS14-02)
#
# Execution-gate layer: judges whether an operation SHOULD be run
# scientifically before it runs, as typed findings + a versioned,
# deterministic PreflightReport. Plain C++20 + jsoncpp; deliberately no
# Qt / QGIS / GDAL so the library stays a testable leaf (sicnu_verifier
# precedent). It consumes the capability knowledge mirror read-only and
# never registers operators, stores or registries of its own.

add_library( sicnu_preflight STATIC
    sha256.cpp
    finding.cpp
    report.cpp
)

target_include_directories( sicnu_preflight PUBLIC ${CMAKE_SOURCE_DIR}/src )

set_target_properties( sicnu_preflight PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

# Same jsoncpp resolution as sicnu_contracts: canonical target when it
# exists, pkg-config fallback otherwise.
if ( TARGET jsoncpp )
    target_link_libraries( sicnu_preflight PUBLIC jsoncpp )
else()
    find_package( PkgConfig QUIET )
    if ( PkgConfig_FOUND )
        pkg_check_modules( JSONCPP QUIET jsoncpp )
    endif()
    if ( JSONCPP_FOUND )
        target_include_directories( sicnu_preflight SYSTEM PUBLIC ${JSONCPP_INCLUDE_DIRS} )
        target_link_libraries( sicnu_preflight PUBLIC ${JSONCPP_LIBRARIES} )
    else()
        find_package( jsoncpp CONFIG QUIET )
        target_link_libraries( sicnu_preflight PUBLIC jsoncpp_lib )
    endif()
endif()

add_library( Sicnu::Preflight ALIAS sicnu_preflight )
