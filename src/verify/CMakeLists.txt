# src/verify — Unified Scientific Verifier (ADR 0172)
#
# The one verification truth for student experiments, agent execution and
# batch acceptance: spec -> engine -> evidence -> report. Plain C++20 +
# jsoncpp; deliberately no Qt and no sicnu targets so the library stays a
# dependency-free leaf (the workflow_run_coordinator library-cycle
# precedent is why this module must never grow a downstream link).
# Future adapters (harness verifyArtifact, exp-rs-prov sidecars, workflow
# nodeFinished, experiment bridges) live OUTSIDE this directory; the
# provider interfaces they will implement are in verify_context.h.

add_library( sicnu_verifier STATIC
    verify_status.cpp
    verify_error_codes.cpp
    verify_sha256.cpp
    verify_types.cpp
)

target_include_directories( sicnu_verifier PUBLIC ${CMAKE_SOURCE_DIR}/src )

set_target_properties( sicnu_verifier PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

# Same jsoncpp resolution as sicnu_contracts: the canonical target when the
# vendored/built target exists, pkg-config fallback otherwise.
if ( TARGET jsoncpp )
    target_link_libraries( sicnu_verifier PUBLIC jsoncpp )
else()
    find_package( PkgConfig QUIET )
    if ( PkgConfig_FOUND )
        pkg_check_modules( JSONCPP QUIET jsoncpp )
    endif()
    if ( JSONCPP_FOUND )
        target_include_directories( sicnu_verifier SYSTEM PUBLIC ${JSONCPP_INCLUDE_DIRS} )
        target_link_libraries( sicnu_verifier PUBLIC ${JSONCPP_LIBRARIES} )
    else()
        find_package( jsoncpp CONFIG QUIET )
        target_link_libraries( sicnu_verifier PUBLIC jsoncpp_lib )
    endif()
endif()
