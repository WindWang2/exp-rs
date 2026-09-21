# src/verification — Unified Scientific Verifier (RS14-10, ADR 0172)
#
# Three-valued verification (Pass / Fail / Indeterminate) over declared
# expectations. Plain C++20 + jsoncpp; deliberately NO Qt, no QGIS, no I/O, no
# clock, no threads — the module must be usable from the CLI and testable
# headless, and a report has to be byte-reproducible across runs.
#
# The one external dependency beyond jsoncpp is the repository's single SHA-256
# implementation (src/geospatial/util/sha256.cpp). It is compiled INTO this
# library rather than linked from a sibling target on purpose: the repo has a
# single-SHA-256 rule, and reaching for it here by source is what keeps that
# rule true instead of inviting a second implementation.

add_library( sicnu_verification STATIC
    # --- core semantics: the lattice, the closed code table, canonical bytes
    status_lattice.cpp
    failure_codes.cpp
    canonical_json.cpp
    verification_types.cpp
    availability.cpp
    evidence.cpp
    spec.cpp
    digest.cpp

    # --- runners: provider dispatch, budget enforcement, roll-up
    check_runner.cpp
    report.cpp

    # --- check families (one TU per family so a family can be read alone)
    checks_state.cpp
    checks_artifact.cpp
    checks_numeric.cpp
    checks_relational.cpp
    checks_provenance.cpp
    checks_reproducibility.cpp
    checks_cross_output.cpp

    # --- composition and presentation
    pack.cpp
    packs_builtin.cpp
    render_teaching.cpp
    render_agent.cpp

    # --- the repository's single sha256, compiled in (see header comment)
    ${CMAKE_SOURCE_DIR}/src/geospatial/util/sha256.cpp
)

target_include_directories( sicnu_verification PUBLIC ${CMAKE_SOURCE_DIR}/src )

set_target_properties( sicnu_verification PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

# packs_builtin.cpp derives packs from the scientific-contract registry, which
# is the single source of truth for operator domains / scale offsets / NoData
# and provenance expectations. Reading the registry is the whole point: a copy
# of those literals here would drift the moment a contract is corrected.
target_link_libraries( sicnu_verification PRIVATE sicnu_contracts )

# Same jsoncpp resolution chain as sicnu_contracts / sicnu_operators: the
# canonical target when the vendored build provides one, pkg-config next, a
# CONFIG package last.
if ( TARGET jsoncpp )
    target_link_libraries( sicnu_verification PUBLIC jsoncpp )
else()
    find_package( PkgConfig QUIET )
    if ( PkgConfig_FOUND )
        pkg_check_modules( JSONCPP QUIET jsoncpp )
    endif()
    if ( JSONCPP_FOUND )
        target_include_directories( sicnu_verification SYSTEM PUBLIC ${JSONCPP_INCLUDE_DIRS} )
        target_link_libraries( sicnu_verification PUBLIC ${JSONCPP_LIBRARIES} )
    else()
        find_package( jsoncpp CONFIG QUIET )
        target_link_libraries( sicnu_verification PUBLIC jsoncpp_lib )
    endif()
endif()
