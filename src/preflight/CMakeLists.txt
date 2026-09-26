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

add_library( Sicnu::Preflight ALIAS sicnu_preflight )
