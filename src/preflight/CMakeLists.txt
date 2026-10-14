# src/preflight — Scientific Preflight Engine (RS14-02)
#
# Execution-gate layer: judges whether an operation SHOULD be run
# scientifically before it runs, as typed findings + a versioned,
# deterministic PreflightReport. Plain C++20 + jsoncpp; the SOURCES stay
# Qt/QGIS/GDAL-free so the library remains a testable leaf (sicnu_verifier
# precedent). It consumes the capability knowledge mirror read-only and
# never registers operators, stores or registries of its own. The link
# closure carries sicnu_data for exactly one reason — the digest paths ride
# the platform canonical-JSON authority (documented at the link edge below).

add_library( sicnu_preflight STATIC
    sha256.cpp
    finding.cpp
    report.cpp
    provider.cpp
    rule.cpp
    engine.cpp
    rules.cpp
    render.cpp
    capability_mirror.cpp
    runtime_adapter.cpp
)

# Slice B: the facts adapter consumes the scientific_state passport types and
# reuses its vocabulary writers (single source of the modality/kind wire
# strings). Both libraries stay Qt/QGIS/GDAL-free jsoncpp leaves; CMake
# resolves the target name at generate time (it is add_subdirectory'ed later
# in the root file — see src/study/bridge for the precedent).
add_library( sicnu_preflight_state_adapter STATIC
    asset_state_adapter.cpp
)
target_link_libraries( sicnu_preflight_state_adapter PUBLIC
    sicnu_preflight
    sicnu_scientific_state
)
set_target_properties( sicnu_preflight_state_adapter PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
)
add_library( Sicnu::PreflightStateAdapter ALIAS sicnu_preflight_state_adapter )

target_include_directories( sicnu_preflight PUBLIC ${CMAKE_SOURCE_DIR}/src )

set_target_properties( sicnu_preflight PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    # Consumers include the shared sicnu_agent library (preflight:check tool
    # shell) — same PIC requirement the recipes libraries carry for the same
    # reason (fix/build precedent 39cd3f862).
    POSITION_INDEPENDENT_CODE ON
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

# R12-E D8 — the ONE recorded exception to the no-Qt/GDAL leaf rule above:
# the report/request digests must ride the platform's single canonical JSON
# authority, sicnu::data::canonicalizeJsonRfc8785 (#1387), not a module-local
# jsoncpp writer; a second serializer is exactly how evidence doubles beyond
# 12 significant digits came to digest differently from every other path.
# PUBLIC, not PRIVATE: the canonicalizer's declaration lives in a Qt header,
# so consumers verifying digests against the authority need the include path
# through this edge. Acyclic by inspection: sicnu_data links Qt6::Core, GDAL,
# SQLite, sicnu_runtime, sicnu_geospatial, Sicnu::Platform and jsoncpp —
# nothing in that closure links back to sicnu_preflight.
target_link_libraries( sicnu_preflight PUBLIC sicnu_data )

add_library( Sicnu::Preflight ALIAS sicnu_preflight )
